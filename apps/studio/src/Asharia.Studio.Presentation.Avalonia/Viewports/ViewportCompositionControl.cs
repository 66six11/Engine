using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Rendering;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Vector3 = System.Numerics.Vector3;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public sealed class ViewportPresentationLayoutProbe : IDisposable
{
    private ViewportCompositionControl? owner_;
    private readonly ulong ticket_;

    internal ViewportPresentationLayoutProbe(ViewportCompositionControl owner, ulong ticket)
    {
        owner_ = owner;
        ticket_ = ticket;
    }

    public bool TryGetExactPixelExtent(out ViewportExtent extent)
    {
        var owner = owner_;
        if (owner is null)
        {
            extent = default;
            return false;
        }
        return owner.TryCapturePresentationLayoutProbe(ticket_, out extent);
    }

    public void Dispose()
    {
        var owner = owner_;
        if (owner is null)
        {
            return;
        }
        owner.EndPresentationLayoutProbe(ticket_);
        owner_ = null;
    }
}

public sealed class ViewportPreparedPresentation : IAsyncDisposable
{
    private readonly TaskCompletionSource<ViewportPresentationTransactionResult> completion_ = new(
        TaskCreationOptions.RunContinuationsAsynchronously);

    internal ViewportPreparedPresentation(
        ViewportCompositionControl owner,
        ViewportPresentationTicket ticket)
    {
        Owner = owner;
        Ticket = ticket;
    }

    public ViewportExtent TargetExtent => Ticket.TargetExtent;

    public Task<ViewportPresentationTransactionResult> Completion => completion_.Task;

    internal ViewportCompositionControl Owner { get; }

    internal ViewportPresentationTicket Ticket { get; }

    internal ulong CandidateRenderedFrames { get; set; }

    public async ValueTask DisposeAsync()
    {
        if (Dispatcher.UIThread.CheckAccess())
        {
            Owner.CancelPreparedPresentation(this);
            return;
        }
        await Dispatcher.UIThread.InvokeAsync(
            () => Owner.CancelPreparedPresentation(this),
            DispatcherPriority.Send);
    }

    internal void Complete(ViewportPresentationTransactionResult result) =>
        completion_.TrySetResult(result);
}

internal readonly record struct ViewportPresentedInteractionContext(
    ViewportSessionId SessionId,
    Guid TargetId,
    ulong TargetRevision,
    ViewportExtent Extent,
    double RenderScaling);

public sealed class ViewportCompositionControl : Control, ICustomHitTest
{
    private enum CompositionCommitResult
    {
        NotSubmittedToConsumer,
        ConsumerAccessed,
        Presented,
    }

    internal sealed class StreamPresentationState
    {
        public StreamPresentationState(
            ViewportRenderStream stream,
            ViewportExtent allocationExtent,
            ulong geometryGeneration)
        {
            Stream = stream;
            AllocationExtent = allocationExtent;
            GeometryGeneration = geometryGeneration;
        }

        public ViewportRenderStream Stream { get; }

        public ViewportExtent AllocationExtent { get; }

        public ulong GeometryGeneration { get; }

        public Dictionary<nint, ImportedSlot> ImportedSlots { get; } = new();

        public HashSet<nint> ExposedSlots { get; } = new();

        public ViewportStreamWorkFence WorkFence { get; } = new();

        public bool IsQuarantined { get; set; }
    }

    internal sealed record ImportedSlot(
        nint NativeSlot,
        ViewportFrameNativeHandles Handles,
        ICompositionImportedGpuImage Image,
        ICompositionImportedGpuSemaphore WaitSemaphore,
        ICompositionImportedGpuSemaphore SignalSemaphore);

    private sealed class PendingCompositionCommit
    {
        public PendingCompositionCommit(
            ViewportExtent extent,
            ulong geometryGeneration,
            CompositionDrawingSurface targetSurface,
            bool publishesVisibleSurface,
            ICompositionImportedGpuImage image,
            ICompositionImportedGpuSemaphore waitSemaphore,
            ICompositionImportedGpuSemaphore signalSemaphore,
            CompositionConsumerAccessTracker accessTracker,
            Func<bool> canPresent,
            Func<bool> tryMarkPresented)
        {
            Extent = extent;
            GeometryGeneration = geometryGeneration;
            TargetSurface = targetSurface;
            PublishesVisibleSurface = publishesVisibleSurface;
            Image = image;
            WaitSemaphore = waitSemaphore;
            SignalSemaphore = signalSemaphore;
            AccessTracker = accessTracker;
            CanPresent = canPresent;
            TryMarkPresented = tryMarkPresented;
        }

        public ViewportExtent Extent { get; }

        public ulong GeometryGeneration { get; }

        public CompositionDrawingSurface TargetSurface { get; }

        public bool PublishesVisibleSurface { get; }

        public ICompositionImportedGpuImage Image { get; }

        public ICompositionImportedGpuSemaphore WaitSemaphore { get; }

        public ICompositionImportedGpuSemaphore SignalSemaphore { get; }

        public CompositionConsumerAccessTracker AccessTracker { get; }

        public Func<bool> CanPresent { get; }

        public Func<bool> TryMarkPresented { get; }

        public TaskCompletionSource<CompositionUpdateCompletion> Completion { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
    }

    internal sealed class PreparedPresentationOperation
    {
        private int cancellationDisposed_;

        public PreparedPresentationOperation(
            ViewportPreparedPresentation handle,
            CompositionDrawingSurface surface,
            StreamPresentationState stream,
            ViewportSession session,
            ViewportPresentationLifetime lifetime,
            ulong attachmentGeneration,
            CancellationTokenSource cancellation)
        {
            Handle = handle;
            Surface = surface;
            Stream = stream;
            Session = session;
            Lifetime = lifetime;
            AttachmentGeneration = attachmentGeneration;
            Cancellation = cancellation;
        }

        public ViewportPreparedPresentation Handle { get; }

        public CompositionDrawingSurface Surface { get; }

        public StreamPresentationState Stream { get; }

        public ViewportSession Session { get; }

        public ViewportPresentationLifetime Lifetime { get; }

        public ulong AttachmentGeneration { get; }

        public CancellationTokenSource Cancellation { get; }

        public ViewportPresentationFrame Frame { get; set; }

        public ViewportRenderSize RenderSize { get; set; }

        public bool OwnsSurface { get; set; } = true;

        public bool OwnsStream { get; set; } = true;

        public void RequestCancellation()
        {
            if (Volatile.Read(ref cancellationDisposed_) == 0)
            {
                try
                {
                    Cancellation.Cancel();
                }
                catch (ObjectDisposedException)
                {
                    // Disposal can win the race after the volatile read. A disposed source is
                    // already terminal, so there is no cancellation work left to request.
                }
            }
        }

        public void DisposeCancellation()
        {
            if (Interlocked.Exchange(ref cancellationDisposed_, 1) == 0)
            {
                Cancellation.Dispose();
            }
        }
    }

    internal sealed class PresentationPublishReceipt
    {
        internal PresentationPublishReceipt(
            PreparedPresentationOperation operation,
            CompositionSurfaceVisual visual,
            CompositionDrawingSurface oldSurface,
            CompositionDrawingSurface oldVisualSurface,
            Vector oldVisualSize,
            float oldVisualOpacity,
            ViewportGeometryChangeSource source,
            IReadOnlyList<StreamPresentationState> replacedStreams)
        {
            Operation = operation;
            Visual = visual;
            OldSurface = oldSurface;
            OldVisualSurface = oldVisualSurface;
            OldVisualSize = oldVisualSize;
            OldVisualOpacity = oldVisualOpacity;
            Source = source;
            ReplacedStreams = replacedStreams;
        }

        internal PreparedPresentationOperation Operation { get; }

        internal CompositionSurfaceVisual Visual { get; }

        internal CompositionDrawingSurface OldSurface { get; }

        internal CompositionDrawingSurface OldVisualSurface { get; }

        internal Vector OldVisualSize { get; }

        internal float OldVisualOpacity { get; }

        internal ViewportGeometryChangeSource Source { get; }

        internal IReadOnlyList<StreamPresentationState> ReplacedStreams { get; }

        internal bool IsFinalized { get; set; }

        internal bool IsRolledBack { get; set; }

        internal bool IsRendered { get; set; }

        internal bool IsQuarantined { get; set; }

        internal ViewportPresentationQuarantineTransferReceipt? QuarantineTransferReceipt
        {
            get;
            set;
        }

        internal string? QuarantineReason { get; set; }
    }

    internal sealed class PublicationOutcomeAmbiguousException : Exception
    {
        internal PublicationOutcomeAmbiguousException(
            PresentationPublishReceipt receipt,
            Exception innerException)
            : base(
                "The viewport presentation could not determine which front the compositor observed.",
                innerException)
        {
            Receipt = receipt;
        }

        internal PresentationPublishReceipt Receipt { get; }
    }

    private readonly record struct NativeReadyWaitResult(
        ViewportFrameTakeResult Take,
        ViewportRenderStreamSnapshot? Snapshot);

    private const string ImageHandleType =
        KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle;
    private const string SemaphoreHandleType =
        KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle;
    private const int MaximumInFlightPresentations = 3;

    public static readonly StyledProperty<ViewportSession?> SessionProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ViewportSession?>(nameof(Session));

    public static readonly StyledProperty<ulong> RevisionTokenProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ulong>(nameof(RevisionToken));

    public static readonly StyledProperty<ViewportPresentationLifetime?> LifetimeProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ViewportPresentationLifetime?>(
            nameof(Lifetime));

    public static readonly StyledProperty<bool> IsRealtimeProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, bool>(
            nameof(IsRealtime),
            defaultValue: true);

    public static readonly DirectProperty<ViewportCompositionControl, ViewportPresentationState>
        StateProperty = AvaloniaProperty.RegisterDirect<
            ViewportCompositionControl,
            ViewportPresentationState>(nameof(State), control => control.State);

    public static readonly DirectProperty<ViewportCompositionControl, string> StatusMessageProperty =
        AvaloniaProperty.RegisterDirect<ViewportCompositionControl, string>(
            nameof(StatusMessage),
            control => control.StatusMessage);

    public static readonly DirectProperty<ViewportCompositionControl, bool> IsDegradedProperty =
        AvaloniaProperty.RegisterDirect<ViewportCompositionControl, bool>(
            nameof(IsDegraded),
            control => control.IsDegraded);

    private readonly ViewportBridge bridge_ = new();
    private readonly ViewportFramePresentationState presentationState_ = new();
    private readonly ViewportPresentationCadenceTracker cadenceTracker_ = new();
    private readonly ViewportGeometryGenerationState geometryState_ = new();
    private readonly ViewportGeometryDiagnosticsTracker geometryDiagnostics_ = new();
    private readonly ViewportPresentationPreparationState presentationPreparation_ = new();
    private readonly ViewportCompositionControlTestHooks? testHooks_;
    private readonly List<Task> retiringStreamTasks_ = new();
    private readonly List<Task> retiringSurfaceTasks_ = new();
    private readonly List<PresentationPublishReceipt> quarantinedPresentations_ = new();
    private readonly List<CompositionDrawingSurface> quarantinedSurfaces_ = new();
    private readonly List<StreamPresentationState> quarantinedStreams_ = new();
    private readonly List<Visual> visibilitySources_ = new();
    private readonly ViewportPresentationEndpointId endpointId_ = new(
        $"viewport-{Guid.NewGuid():N}");
    private ViewportPresentationStateDiagnosticTracker? stateDiagnostics_;
    private CompositionSurfaceVisual? compositionVisual_;
    private CompositionDrawingSurface? surface_;
    private ICompositionGpuInterop? interop_;
    private ViewportPresentationLifetime? subscribedLifetime_;
    private ViewportSession? subscribedSession_;
    private TopLevel? topLevel_;
    private StreamPresentationState? activeStream_;
    private StreamPresentationState? desiredStream_;
    private PreparedPresentationOperation? preparingPresentation_;
    private PreparedPresentationOperation? preparedPresentation_;
    private Task detachTask_;
    private ViewportPresentationState state_ = ViewportPresentationState.Detached;
    private string statusMessage_ = "Scene View is detached.";
    private bool isDegraded_;
    private bool isAttached_;
    private bool isFrameQueued_;
    private bool queuedFrameUsesEarlyAdmission_;
    private bool isCompositionCommitQueued_;
    private bool wasEffectivelyVisible_;
    private PendingCompositionCommit? pendingCompositionCommit_;
    private ViewportRenderSize lastPresentedSize_;
    private ViewportExtent lastPresentedPanelExtent_;
    private ViewportPresentationFrame lastPresentedFrame_;
    private ulong exactExtentPresentedFrames_;
    private ulong rejectedNonExactCandidates_;
    private ulong queuedFrameTicket_;
    private ulong compositionCommitTicket_;
    private ulong generation_;
    private long candidateSurfaceCreateAttempts_;
    private long candidateSurfacesCreated_;
    private long candidateStreamOpenAttempts_;
    private long candidateStreamsOpened_;
    private long candidateNativeSubmissions_;
    private long candidateLeasesAcquired_;
    private long candidateImageImportAttempts_;
    private long candidateImagesImported_;
    private long candidateSurfaceUpdateAttempts_;
    private long candidateCleanupCompletions_;

    public ViewportCompositionControl()
        : this(Task.CompletedTask, testHooks: null)
    {
    }

    internal ViewportCompositionControl(Task precedingDetach)
        : this(precedingDetach, testHooks: null)
    {
    }

    internal ViewportCompositionControl(
        Task precedingDetach,
        ViewportCompositionControlTestHooks? testHooks)
    {
        ArgumentNullException.ThrowIfNull(precedingDetach);
        detachTask_ = precedingDetach;
        testHooks_ = testHooks;
        ClipToBounds = true;
    }

    // The compositor child visual has no Avalonia draw list of its own, so declare the viewport's
    // local bounds as the routed-input surface without adding a second visual layer.
    bool ICustomHitTest.HitTest(Point point) => new Rect(Bounds.Size).Contains(point);

    public ViewportSession? Session
    {
        get => GetValue(SessionProperty);
        set => SetValue(SessionProperty, value);
    }

    public ulong RevisionToken
    {
        get => GetValue(RevisionTokenProperty);
        set => SetValue(RevisionTokenProperty, value);
    }

    public ViewportPresentationLifetime? Lifetime
    {
        get => GetValue(LifetimeProperty);
        set => SetValue(LifetimeProperty, value);
    }

    public bool IsRealtime
    {
        get => GetValue(IsRealtimeProperty);
        set => SetValue(IsRealtimeProperty, value);
    }

    public ViewportPresentationMetrics PresentationMetrics => cadenceTracker_.Capture();

    public ViewportPresentationGeometryMetrics PresentationGeometryMetrics => new(
        exactExtentPresentedFrames_,
        rejectedNonExactCandidates_,
        lastPresentedSize_,
        lastPresentedPanelExtent_,
        geometryState_.CurrentGeneration,
        geometryState_.SurfaceGeneration,
        geometryState_.HasExactSurface);

    internal bool TryCapturePresentedInteractionContext(
        out ViewportPresentedInteractionContext context)
    {
        Dispatcher.UIThread.VerifyAccess();
        context = default;
        var renderScaling = topLevel_?.RenderScaling ?? 0;
        if (!isAttached_ || State != ViewportPresentationState.Ready || IsDegraded ||
            Session is not { } session || !lastPresentedFrame_.SessionId.IsValid ||
            !session.CanPresentPublishedFrame(
                lastPresentedFrame_.Sequence,
                lastPresentedFrame_.TargetRevision) ||
            !TryGetRenderSize(out var currentSize) ||
            currentSize.LogicalExtent != lastPresentedSize_.LogicalExtent ||
            currentSize.LogicalExtent != lastPresentedPanelExtent_ || renderScaling <= 0)
        {
            return false;
        }

        context = new ViewportPresentedInteractionContext(
            lastPresentedFrame_.SessionId,
            lastPresentedFrame_.TargetId,
            lastPresentedFrame_.TargetRevision,
            currentSize.LogicalExtent,
            renderScaling);
        return true;
    }

    public ViewportResizeMeasurementToken BeginResizeMeasurement() =>
        geometryDiagnostics_.BeginMeasurement(
            geometryState_.CurrentGeneration,
            Stopwatch.GetTimestamp());

    public ViewportPresentationLayoutProbe BeginPresentationLayoutProbe()
    {
        Dispatcher.UIThread.VerifyAccess();
        return new ViewportPresentationLayoutProbe(
            this,
            presentationPreparation_.BeginLayoutProbe());
    }

    internal ViewportPresentationTestSnapshot CapturePresentationTestSnapshot()
    {
        Dispatcher.UIThread.VerifyAccess();
        var visualSurface = compositionVisual_?.Surface;
        var displayedOperation = preparedPresentation_ is { } prepared &&
                                 ReferenceEquals(visualSurface, prepared.Surface)
            ? prepared
            : quarantinedPresentations_.LastOrDefault(receipt =>
                ReferenceEquals(visualSurface, receipt.Operation.Surface))?.Operation;
        var visualSurfaceExtent = displayedOperation is not null
            ? displayedOperation.RenderSize.AllocationExtent
            : ReferenceEquals(compositionVisual_?.Surface, surface_)
                ? lastPresentedSize_.AllocationExtent
                : default;
        var candidateExtent = preparedPresentation_?.Handle.TargetExtent ??
                              preparingPresentation_?.Handle.TargetExtent ??
                              default;
        return new ViewportPresentationTestSnapshot(
            preparingPresentation_ is not null,
            preparedPresentation_ is not null,
            retiringStreamTasks_.Count,
            retiringSurfaceTasks_.Count,
            quarantinedPresentations_.Count,
            quarantinedStreams_.Count,
            quarantinedSurfaces_.Count,
            Lifetime?.QuarantinedFrameCount ?? 0,
            compositionVisual_?.Opacity ?? 0,
            compositionVisual_?.Surface,
            compositionVisual_?.Size ?? default,
            visualSurfaceExtent,
            lastPresentedSize_.AllocationExtent,
            candidateExtent,
            geometryState_.CurrentExtent,
            geometryState_.CurrentGeneration,
            geometryState_.SurfaceGeneration,
            geometryState_.HasExactSurface,
            presentationState_.LastPresentedSequence,
            Interlocked.Read(ref candidateSurfaceCreateAttempts_),
            Interlocked.Read(ref candidateSurfacesCreated_),
            Interlocked.Read(ref candidateStreamOpenAttempts_),
            Interlocked.Read(ref candidateStreamsOpened_),
            Interlocked.Read(ref candidateNativeSubmissions_),
            Interlocked.Read(ref candidateLeasesAcquired_),
            Interlocked.Read(ref candidateImageImportAttempts_),
            Interlocked.Read(ref candidateImagesImported_),
            Interlocked.Read(ref candidateSurfaceUpdateAttempts_),
            Interlocked.Read(ref candidateCleanupCompletions_));
    }

    public async Task<ViewportPreparedPresentation> PreparePresentationAsync(
        ViewportExtent targetExtent,
        CancellationToken cancellationToken = default)
    {
        Dispatcher.UIThread.VerifyAccess();
        cancellationToken.ThrowIfCancellationRequested();
        if (targetExtent.Width == 0 || targetExtent.Height == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(targetExtent));
        }
        if (!isAttached_ || compositionVisual_ is not { } visual || interop_ is not { } interop ||
            Session is not { } session || Lifetime is not { } lifetime ||
            !lifetime.IsAcceptingFrames || !geometryState_.HasExactSurface ||
            !TryGetRenderSize(out var currentSize) ||
            currentSize.LogicalExtent != geometryState_.CurrentExtent)
        {
            throw new InvalidOperationException(
                "A presentation transaction requires an attached viewport with a current visible front surface.");
        }
        if (targetExtent == currentSize.LogicalExtent)
        {
            throw new ArgumentException(
                "The prepared presentation extent must differ from the committed front extent.",
                nameof(targetExtent));
        }
        if (preparingPresentation_ is not null || preparedPresentation_ is not null)
        {
            throw new InvalidOperationException("A viewport presentation is already pending.");
        }

        var ticket = presentationPreparation_.BeginPreparation(
            targetExtent,
            geometryState_.CurrentGeneration);
        var handle = new ViewportPreparedPresentation(this, ticket);
        // The retained front surface is independent from its producer stream once Avalonia has
        // consumed the imported image. Stop the obsolete producer as soon as a replacement
        // transaction starts so its three native slots do not serialize candidate creation.
        RetireCurrentStreams();
        CompositionDrawingSurface? acquiredSurface = null;
        StreamPresentationState? acquiredStream = null;
        CancellationTokenSource? acquiredCancellation = null;
        PreparedPresentationOperation operation;
        Task preparation;
        try
        {
            Interlocked.Increment(ref candidateSurfaceCreateAttempts_);
            if (testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.BeforeSurfaceCreate,
                    cancellationToken);
            }
            acquiredSurface = visual.Compositor.CreateDrawingSurface();
            Interlocked.Increment(ref candidateSurfacesCreated_);
            Interlocked.Increment(ref candidateStreamOpenAttempts_);
            if (testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.BeforeStreamOpen,
                    cancellationToken);
            }
            var opened = bridge_.OpenStream(CreateCompatibility(interop));
            if (!opened.Succeeded)
            {
                throw new InvalidOperationException(opened.Failure!.Message);
            }
            Interlocked.Increment(ref candidateStreamsOpened_);

            acquiredStream = new StreamPresentationState(
                opened.Stream!,
                targetExtent,
                ticket.CandidateGeometryGeneration);
            acquiredCancellation = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken);
            operation = new PreparedPresentationOperation(
                handle,
                acquiredSurface,
                acquiredStream,
                session,
                lifetime,
                generation_,
                acquiredCancellation);
            preparingPresentation_ = operation;
            preparation = PreparePresentationCoreAsync(
                operation,
                acquiredCancellation.Token);
            acquiredStream.WorkFence.TrackPresentation(preparation);
        }
        catch
        {
            presentationPreparation_.TryCancel(ticket);
            preparingPresentation_ = null;
            acquiredCancellation?.Cancel();
            if (acquiredStream is null)
            {
                acquiredCancellation?.Dispose();
                acquiredSurface?.Dispose();
                if (acquiredSurface is not null)
                {
                    Interlocked.Increment(ref candidateCleanupCompletions_);
                }
            }
            else
            {
                var retirement = RetireAcquiredPresentationAsync(
                    acquiredStream,
                    acquiredSurface,
                    acquiredCancellation);
                TrackRetiringSurfaceTask(retirement);
            }
            ResumeFrontPresentation();
            throw;
        }
        try
        {
            await preparation;
            acquiredCancellation.Token.ThrowIfCancellationRequested();
            if (!ReferenceEquals(preparingPresentation_, operation) ||
                !presentationPreparation_.TryMarkPrepared(ticket))
            {
                throw new OperationCanceledException(
                    "The viewport presentation preparation was invalidated.",
                    acquiredCancellation.Token);
            }

            preparingPresentation_ = null;
            preparedPresentation_ = operation;
            handle.CandidateRenderedFrames = operation.Stream.Stream.Poll().RenderedFrames;
            return handle;
        }
        catch (Exception exception)
        {
            presentationPreparation_.TryCancel(ticket);
            if (ReferenceEquals(preparingPresentation_, operation))
            {
                preparingPresentation_ = null;
            }
            QueueUncommittedPresentationRetirement(operation);
            ResumeFrontPresentation();
            if (exception is ViewportPresentationUnsupportedFeatureException unsupported &&
                IsCurrent(operation.AttachmentGeneration))
            {
                SetDegraded(ViewportPresentationState.Unsupported, unsupported.Message);
            }
            else if (exception is not (OperationCanceledException or
                         ViewportPresentationRecoverableException) &&
                     IsCurrent(operation.AttachmentGeneration))
            {
                SetDegraded(
                    ViewportPresentationState.RenderFailed,
                    $"Viewport presentation preparation failed: {exception.Message}");
            }
            throw;
        }
        finally
        {
            acquiredStream.WorkFence.UntrackPresentation(preparation);
        }
    }

    public bool ArmPreparedPresentation(ViewportPreparedPresentation prepared)
    {
        ArgumentNullException.ThrowIfNull(prepared);
        Dispatcher.UIThread.VerifyAccess();
        if (!ReferenceEquals(prepared.Owner, this) ||
            preparedPresentation_ is not { } operation ||
            !ReferenceEquals(operation.Handle, prepared))
        {
            return false;
        }
        if (!IsPreparedPresentationCurrent(operation))
        {
            presentationPreparation_.TryCancel(prepared.Ticket);
            preparedPresentation_ = null;
            prepared.Complete(ViewportPresentationTransactionResult.Invalidated);
            QueueUncommittedPresentationRetirement(operation);
            ResumeFrontPresentation();
            return false;
        }
        if (!presentationPreparation_.TryArm(prepared.Ticket))
        {
            return false;
        }
        return true;
    }

    public bool CancelPreparedPresentation(ViewportPreparedPresentation prepared)
    {
        ArgumentNullException.ThrowIfNull(prepared);
        Dispatcher.UIThread.VerifyAccess();
        if (!ReferenceEquals(prepared.Owner, this) ||
            preparedPresentation_ is not { } operation ||
            !ReferenceEquals(operation.Handle, prepared) ||
            !presentationPreparation_.TryCancel(prepared.Ticket))
        {
            return false;
        }

        preparedPresentation_ = null;
        prepared.Complete(ViewportPresentationTransactionResult.Cancelled);
        QueueUncommittedPresentationRetirement(operation);
        ResumeFrontPresentation();
        return true;
    }

    public ViewportResizePresentationMetrics CaptureResizeMeasurement(
        ViewportResizeMeasurementToken token) =>
        geometryDiagnostics_.Capture(
            token,
            geometryState_.CurrentGeneration,
            geometryState_.HasExactSurface,
            Stopwatch.GetTimestamp());

    public ViewportPresentationState State
    {
        get => state_;
        private set => SetAndRaise(StateProperty, ref state_, value);
    }

    public string StatusMessage
    {
        get => statusMessage_;
        private set => SetAndRaise(StatusMessageProperty, ref statusMessage_, value);
    }

    public bool IsDegraded
    {
        get => isDegraded_;
        private set => SetAndRaise(IsDegradedProperty, ref isDegraded_, value);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached_ = true;
        SynchronizeSessionSubscription();
        SynchronizeLifetimeSubscription();
        SynchronizeVisibilitySubscriptions();
        topLevel_ = TopLevel.GetTopLevel(this);
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged += OnScalingChanged;
        }
        _ = SynchronizeGeometryGeneration(ViewportGeometryChangeSource.Attachment);

        var generation = ++generation_;
        presentationState_.Reset(generation);
        cadenceTracker_.Reset();
        lastPresentedSize_ = default;
        lastPresentedPanelExtent_ = default;
        lastPresentedFrame_ = default;
        exactExtentPresentedFrames_ = 0;
        rejectedNonExactCandidates_ = 0;
        _ = AttachAsync(generation, detachTask_);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        preparingPresentation_?.RequestCancellation();
        var presentationOperations = new[] { preparingPresentation_, preparedPresentation_ }
            .Where(static operation => operation is not null)
            .Cast<PreparedPresentationOperation>()
            .Distinct()
            .ToArray();
        foreach (var operation in presentationOperations)
        {
            operation.Handle.Complete(ViewportPresentationTransactionResult.Invalidated);
            operation.DisposeCancellation();
        }
        preparingPresentation_ = null;
        preparedPresentation_ = null;
        presentationPreparation_.Reset();
        ClearVisibilitySubscriptions();
        isFrameQueued_ = false;
        queuedFrameUsesEarlyAdmission_ = false;
        queuedFrameTicket_++;
        pendingCompositionCommit_?.Completion.TrySetResult(
            CompositionUpdateCompletion.NotSubmittedToConsumer);
        pendingCompositionCommit_ = null;
        isCompositionCommitQueued_ = false;
        compositionCommitTicket_++;
        geometryState_.Invalidate();
        geometryDiagnostics_.Reset();
        presentationState_.Reset(++generation_);
        SynchronizeSessionSubscription();
        SynchronizeLifetimeSubscription();
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged -= OnScalingChanged;
            topLevel_ = null;
        }

        interop_ = null;
        var visual = compositionVisual_;
        ElementComposition.SetElementChildVisual(this, null);
        var removalProcessed = visual?.Compositor.RequestCompositionBatchCommitAsync().Processed ??
            Task.CompletedTask;
        TransferQuarantinedPresentationOwnership();
        var processOwnedSurfaces = quarantinedSurfaces_.ToHashSet();
        var processOwnedStreams = quarantinedStreams_.ToHashSet();
        var surfaces = presentationOperations
            .Where(static operation => operation.OwnsSurface)
            .Select(static operation => operation.Surface)
            .Append(surface_)
            .Where(static surface => surface is not null)
            .Cast<CompositionDrawingSurface>()
            .Where(surface => !processOwnedSurfaces.Contains(surface))
            .Distinct()
            .ToArray();
        foreach (var operation in presentationOperations)
        {
            operation.OwnsSurface = false;
        }
        surface_ = null;
        compositionVisual_ = null;

        var streams = DistinctStreams(activeStream_, desiredStream_)
            .Concat(presentationOperations
                .Where(static operation => operation.OwnsStream)
                .Select(static operation => operation.Stream))
            .Where(stream => !processOwnedStreams.Contains(stream))
            .Distinct()
            .ToArray();
        foreach (var operation in presentationOperations)
        {
            operation.OwnsStream = false;
        }
        activeStream_ = null;
        desiredStream_ = null;
        quarantinedPresentations_.Clear();
        quarantinedSurfaces_.Clear();
        quarantinedStreams_.Clear();
        var retirements = retiringStreamTasks_.ToList();
        retirements.AddRange(retiringSurfaceTasks_);
        retirements.AddRange(streams.Select(BeginRetireStream));
        SetStatus(ViewportPresentationState.Draining, "Scene View presentation is draining.");
        var admission = Lifetime?.BeginCleanup();
        detachTask_ = DrainDetachedPresentationAsync(
            surfaces,
            removalProcessed,
            retirements.Distinct().ToArray(),
            admission);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            if (TryHandlePresentationLayoutChange(ViewportGeometryChangeSource.Bounds))
            {
                return;
            }
            var geometryChanged = SynchronizeGeometryGeneration(
                ViewportGeometryChangeSource.Bounds);
            UpdateVisualPlacement();
            InvalidatePresentation(
                resetPresentationEpoch: false,
                preferEarlyAdmission: geometryChanged);
        }
        else if (change.Property == SessionProperty)
        {
            SynchronizeSessionSubscription();
            _ = TryInvalidateExposedSession();
            InvalidatePresentation(resetPresentationEpoch: true);
        }
        else if (change.Property == RevisionTokenProperty)
        {
            InvalidatePresentation(resetPresentationEpoch: false);
        }
        else if (change.Property == LifetimeProperty)
        {
            SynchronizeLifetimeSubscription();
            _ = TryInvalidateExposedSession();
            InvalidatePresentation(resetPresentationEpoch: true);
        }
        else if (change.Property == IsRealtimeProperty &&
                 CanScheduleAutomaticRealtime() &&
                 Session is { } realtimeSession)
        {
            if (TryInvalidateOpenSession(
                    realtimeSession,
                    ViewportInvalidationReason.Realtime))
            {
                InvalidatePresentation(resetPresentationEpoch: false);
            }
        }
    }

    private async Task AttachAsync(ulong generation, Task precedingDetach)
    {
        try
        {
            await precedingDetach;
            await Dispatcher.UIThread.InvokeAsync(() => AttachAndProbeAsync(generation));
        }
        catch (Exception exception)
        {
            if (IsCurrent(generation))
            {
                SetDegraded(
                    ViewportPresentationState.RenderFailed,
                    $"Scene View attachment failed: {exception.Message}");
            }
        }
    }

    private async Task AttachAndProbeAsync(ulong generation)
    {
        if (!IsCurrent(generation))
        {
            return;
        }

        var elementVisual = ElementComposition.GetElementVisual(this);
        var compositor = elementVisual?.Compositor ?? Compositor.TryGetDefaultCompositor();
        if (compositor is null)
        {
            SetDegraded(
                ViewportPresentationState.Unsupported,
                "Avalonia composition is unavailable for this Scene View.");
            return;
        }

        surface_ = compositor.CreateDrawingSurface();
        compositionVisual_ = compositor.CreateSurfaceVisual();
        compositionVisual_.Surface = surface_;
        ElementComposition.SetElementChildVisual(this, compositionVisual_);
        UpdateVisualPlacement();
        SetStatus(ViewportPresentationState.Probing, "Checking Vulkan composition support.");

        var interop = await compositor.TryGetCompositionGpuInterop();
        if (!IsCurrent(generation))
        {
            return;
        }
        if (interop is null || interop.IsLost ||
            !interop.SupportedImageHandleTypes.Contains(ImageHandleType, StringComparer.Ordinal) ||
            !interop.SupportedSemaphoreTypes.Contains(SemaphoreHandleType, StringComparer.Ordinal))
        {
            SetDegraded(
                ViewportPresentationState.Unsupported,
                "This compositor does not support Vulkan opaque NT image and semaphore import.");
            return;
        }

        interop_ = interop;
        if (Session is not { } session)
        {
            SetStatus(
                ViewportPresentationState.WaitingForDocument,
                "Create or open a project to display its default scene.");
            return;
        }
        SetStatus(ViewportPresentationState.Ready, "Scene View presentation is ready.");
        if (TryInvalidateExposedSession())
        {
            QueueFrame();
        }
    }

    private void InvalidatePresentation(
        bool resetPresentationEpoch,
        bool preferEarlyAdmission = false)
    {
        if (!preferEarlyAdmission)
        {
            InvalidatePendingPresentation();
        }
        if (!isAttached_ || interop_ is null)
        {
            return;
        }
        if (resetPresentationEpoch)
        {
            presentationState_.Reset(++generation_);
            lastPresentedFrame_ = default;
            geometryState_.InvalidateSurface();
            geometryDiagnostics_.RecordGeneration(
                geometryState_.CurrentGeneration,
                geometryState_.CurrentExtent,
                ViewportGeometryChangeSource.PresentationIdentity,
                Stopwatch.GetTimestamp());
            UpdateVisualPlacement();
            RetireCurrentStreams();
        }
        if (Session is null)
        {
            SetStatus(
                ViewportPresentationState.WaitingForDocument,
                "Create or open a project to display its default scene.");
            return;
        }
        if (!IsEffectivelyVisible)
        {
            return;
        }
        QueueFrame(preferEarlyAdmission);
    }

    private void InvalidatePendingPresentation()
    {
        if (preparingPresentation_ is { } preparing)
        {
            presentationPreparation_.TryCancel(preparing.Handle.Ticket);
            preparing.RequestCancellation();
        }
        if (preparedPresentation_ is not { } prepared)
        {
            return;
        }

        presentationPreparation_.TryCancel(prepared.Handle.Ticket);
        preparedPresentation_ = null;
        prepared.Handle.Complete(ViewportPresentationTransactionResult.Invalidated);
        QueueUncommittedPresentationRetirement(prepared);
    }

    private void QueueFrame(bool preferEarlyAdmission = false)
    {
        if (!isAttached_ || interop_ is null || compositionVisual_ is not { } visual ||
            presentationPreparation_.HasPreparation)
        {
            return;
        }
        var compositor = visual.Compositor;
        if (isFrameQueued_)
        {
            if (preferEarlyAdmission && !queuedFrameUsesEarlyAdmission_)
            {
                // A pending compositor-cadence request may be promoted to the earlier Render
                // dispatcher boundary. Both callbacks share one ticket; whichever runs first
                // consumes the latch and the other becomes a no-op.
                queuedFrameUsesEarlyAdmission_ = true;
                var promotedTicket = queuedFrameTicket_;
                Dispatcher.UIThread.Post(
                    () => PublishLatestFrame(
                        promotedTicket,
                        generation_,
                        compositor,
                        queuedEarlyAdmission: true),
                    DispatcherPriority.Render);
            }
            return;
        }
        isFrameQueued_ = true;
        queuedFrameUsesEarlyAdmission_ = preferEarlyAdmission;
        var ticket = ++queuedFrameTicket_;
        var generation = generation_;
        if (preferEarlyAdmission)
        {
            // Bounds changes are coalesced by this one-shot Render-priority latch. Native can
            // start the latest exact-size candidate before the next composition callback, while
            // the eventual surface update still revalidates geometry at the compositor boundary.
            Dispatcher.UIThread.Post(
                () => PublishLatestFrame(
                    ticket,
                    generation_,
                    compositor,
                    queuedEarlyAdmission: true),
                DispatcherPriority.Render);
            return;
        }
        // Realtime and retry cadence remain compositor-paced; no fixed-rate UI timer is used.
        compositor.RequestCompositionUpdate(
            () => PublishLatestFrame(
                ticket,
                generation,
                compositor,
                queuedEarlyAdmission: false));
    }

    private void PublishLatestFrame(
        ulong queuedTicket,
        ulong queuedGeneration,
        Compositor queuedCompositor,
        bool queuedEarlyAdmission)
    {
        // A callback from a detached visual must not clear a newer visual's queue latch.
        if (!isFrameQueued_ || queuedFrameTicket_ != queuedTicket)
        {
            return;
        }
        isFrameQueued_ = false;
        queuedFrameUsesEarlyAdmission_ = false;
        var publishGeneration = queuedEarlyAdmission ? generation_ : queuedGeneration;
        if ((!queuedEarlyAdmission && queuedGeneration != generation_) ||
            compositionVisual_ is not { } visual ||
            !ReferenceEquals(visual.Compositor, queuedCompositor))
        {
            // Generation changes are state replacement, not cancellation of the latest frame.
            // Re-arm the same admission boundary for the current visual.
            QueueFrame(queuedEarlyAdmission);
            return;
        }
        if (!IsCurrent(publishGeneration) || !IsEffectivelyVisible || interop_ is null ||
            surface_ is null || presentationPreparation_.HasPreparation ||
            Session is not { } session || Lifetime is not { } lifetime ||
            !lifetime.IsAcceptingFrames ||
            !TryGetRenderSize(out var renderSize) ||
            !session.TryPublishLatest(renderSize, out var request))
        {
            return;
        }
        testHooks_?.ObserveRequest(request);

        var stream = EnsureDesiredStream(
            request.AllocationExtent,
            geometryState_.CurrentGeneration,
            interop_,
            out var streamFailure);
        if (stream is null)
        {
            session.RetryPublishedFrame(request);
            HandleSubmissionFailure(streamFailure!);
            return;
        }
        var submitted = stream.Stream.SubmitLatest(
            request,
            testHooks_?.DiagnosticOverlay ?? ViewportRenderDiagnosticOverlay.None);
        if (!submitted.Succeeded)
        {
            if (submitted.Failure!.Kind != ViewportFrameFailureKind.UnsupportedFeature)
            {
                session.RetryPublishedFrame(request);
            }
            HandleSubmissionFailure(submitted.Failure!);
            return;
        }
        EnsurePumpRunning(publishGeneration, lifetime);
        if (CanScheduleAutomaticRealtime())
        {
            // RequestCompositionUpdate called from inside this callback is queued for the next
            // commit. That overlaps native production of frame N+1 with Avalonia consuming frame
            // N, so a 60 Hz compositor can sustain one scene frame per tick after warm-up.
            if (TryInvalidateOpenSession(
                    session,
                    ViewportInvalidationReason.Realtime))
            {
                QueueFrame();
            }
        }
    }

    private StreamPresentationState? EnsureDesiredStream(
        ViewportExtent allocationExtent,
        ulong geometryGeneration,
        ICompositionGpuInterop interop,
        out ViewportFrameFailure? failure)
    {
        failure = null;
        if (desiredStream_ is { } desired &&
            desired.AllocationExtent == allocationExtent &&
            desired.GeometryGeneration == geometryGeneration)
        {
            return desired;
        }
        var replacedStreams = DistinctStreams(activeStream_, desiredStream_).ToArray();
        activeStream_ = null;
        desiredStream_ = null;
        foreach (var replacedStream in replacedStreams)
        {
            // An exact-size viewport never presents the old generation through crop or stretch.
            // Retire it as soon as the panel requests a different extent so its slots do not hold
            // the global packet budget while rapid resize candidates replace one another.
            _ = BeginRetireStream(replacedStream);
        }

        var opened = bridge_.OpenStream(CreateCompatibility(interop));
        if (!opened.Succeeded)
        {
            failure = opened.Failure!;
            return null;
        }
        desiredStream_ = new StreamPresentationState(
            opened.Stream!,
            allocationExtent,
            geometryGeneration);
        return desiredStream_;
    }

    private void HandleSubmissionFailure(ViewportFrameFailure failure)
    {
        if (failure.Kind == ViewportFrameFailureKind.Backpressure)
        {
            // Retry at the next compositor commit. This keeps retry cadence bounded while letting
            // an old allocation stream finish retirement without requiring another Bounds event.
            QueueFrame();
            return;
        }
        SetAcquireFailure(failure);
    }

    private void EnsurePumpRunning(ulong generation, ViewportPresentationLifetime lifetime)
    {
        if (desiredStream_ is not { } stream ||
            !stream.WorkFence.TryStartPump(
                () => PumpFramesAsync(stream, generation, lifetime),
                out var pump))
        {
            return;
        }
        _ = RestartPumpAfterCompletionAsync(stream, pump, generation, lifetime);
    }

    private async Task RestartPumpAfterCompletionAsync(
        StreamPresentationState stream,
        Task pump,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        try
        {
            await pump;
        }
        catch
        {
            return;
        }

        if (Dispatcher.UIThread.CheckAccess())
        {
            RestartPumpIfNeeded(stream, pump, generation, lifetime);
            return;
        }
        await Dispatcher.UIThread.InvokeAsync(
            () => RestartPumpIfNeeded(stream, pump, generation, lifetime),
            DispatcherPriority.Render);
    }

    private void RestartPumpIfNeeded(
        StreamPresentationState stream,
        Task observedPump,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        if (!IsCurrent(generation) || !ReferenceEquals(Lifetime, lifetime) ||
            stream.WorkFence.IsRetiring || !ReferenceEquals(desiredStream_, stream) ||
            !ReferenceEquals(stream.WorkFence.PumpTask, observedPump) ||
            !observedPump.IsCompleted ||
            stream.WorkFence.PresentationCount >= MaximumInFlightPresentations)
        {
            return;
        }
        try
        {
            var snapshot = stream.Stream.Poll();
            if (snapshot.HasPendingLatest || snapshot.HasReadyFrame || snapshot.RenderExecuting)
            {
                EnsurePumpRunning(generation, lifetime);
            }
        }
        catch (Exception exception)
        {
            SetDegraded(
                ViewportPresentationState.RenderFailed,
                $"Scene View stream polling failed: {exception.Message}");
        }
    }

    private async Task PumpFramesAsync(
        StreamPresentationState stream,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        try
        {
            await PumpFramesCoreAsync(stream, generation, lifetime);
        }
        catch (Exception exception)
        {
            if (IsCurrent(generation))
            {
                SetDegraded(
                    ViewportPresentationState.RenderFailed,
                    $"Scene View stream pump failed: {exception.Message}");
            }
        }
    }

    private async Task PumpFramesCoreAsync(
        StreamPresentationState stream,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        while (IsCurrent(generation) && ReferenceEquals(Lifetime, lifetime) &&
               !stream.WorkFence.IsRetiring && ReferenceEquals(desiredStream_, stream))
        {
            if (stream.WorkFence.PresentationCount >= MaximumInFlightPresentations)
            {
                return;
            }

            // Only the native polling wait leaves the UI context. The continuation below
            // returns to Avalonia once a frame or terminal stream state is available.
            var wait = await WaitForReadyFrameAsync(stream.Stream);
            var taken = wait.Take;
            if (!taken.Succeeded)
            {
                SetAcquireFailure(taken.Failure!);
                return;
            }
            if (!taken.HasFrame)
            {
                var snapshot = wait.Snapshot!;
                if (snapshot.Lifecycle == ViewportRenderStreamLifecycle.Faulted)
                {
                    SetDegraded(
                        ViewportPresentationState.RenderFailed,
                        "Native viewport stream entered a faulted state.");
                }
                return;
            }

            var lease = taken.Lease!;
            stream.ExposedSlots.Add(lease.SlotIdentity);
            try
            {
                testHooks_?.ObserveLease(lease);
            }
            catch
            {
                ReleaseNotSubmittedOrQuarantine(stream, lease, lifetime);
                throw;
            }
            if (stream.WorkFence.IsRetiring || !IsCurrent(generation) ||
                !ReferenceEquals(Lifetime, lifetime) ||
                !ReferenceEquals(desiredStream_, stream))
            {
                ReleaseNotSubmittedOrQuarantine(stream, lease, lifetime);
                return;
            }
            // Avalonia's composition swapchain keeps multiple LastPresent tasks in flight.
            // Awaiting each surface update here would collapse the native three-slot stream
            // back to a one-frame pipeline and make fast resize visibly freeze.
            var presentation = PresentAndReleaseFrameAsync(
                stream,
                lease,
                generation,
                lifetime);
            stream.WorkFence.TrackPresentation(presentation);
            _ = ObservePresentationCompletionAsync(
                stream,
                presentation,
                generation,
                lifetime);
        }
    }

    private async Task ObservePresentationCompletionAsync(
        StreamPresentationState stream,
        Task presentation,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        Exception? failure = null;
        try
        {
            await presentation;
        }
        catch (Exception exception)
        {
            failure = exception;
        }

        void CompleteOnUiThread()
        {
            stream.WorkFence.UntrackPresentation(presentation);
            if (failure is not null && IsCurrent(generation))
            {
                SetDegraded(
                    ViewportPresentationState.RenderFailed,
                    $"Scene View presentation failed: {failure.Message}");
                return;
            }
            RestartPumpIfNeeded(stream, stream.WorkFence.PumpTask, generation, lifetime);
        }

        if (Dispatcher.UIThread.CheckAccess())
        {
            CompleteOnUiThread();
        }
        else
        {
            await Dispatcher.UIThread.InvokeAsync(
                CompleteOnUiThread,
                DispatcherPriority.Render);
        }
    }

    private async Task PresentAndReleaseFrameAsync(
        StreamPresentationState stream,
        ViewportFrameLease lease,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        var releaseLease = true;
        try
        {
            var result = await PresentReadyFrameAsync(
                stream,
                lease,
                generation,
                lifetime);
            if (result is CompositionCommitResult.ConsumerAccessed or
                CompositionCommitResult.Presented)
            {
                try
                {
                    lease.Release(ViewportFrameCompletionKind.ConsumerAccessed);
                    releaseLease = false;
                }
                catch
                {
                    stream.IsQuarantined = true;
                    lease.Quarantine();
                    stream.ImportedSlots.TryGetValue(
                        lease.SlotIdentity,
                        out var imported);
                    lifetime.QuarantineFrame(
                        lease,
                        imported?.Image,
                        imported?.WaitSemaphore,
                        imported?.SignalSemaphore);
                    releaseLease = false;
                    throw;
                }
            }
            if (result == CompositionCommitResult.Presented)
            {
                var promoted = PromoteStream(stream);
                if (promoted && CanScheduleAutomaticRealtime() &&
                    IsCurrent(generation) &&
                    ReferenceEquals(desiredStream_, stream) &&
                    Session is { } session)
                {
                    // Candidate generations submit only one frame. Resume compositor-paced
                    // realtime production after their first exact surface update succeeds.
                    if (TryInvalidateOpenSession(
                            session,
                            ViewportInvalidationReason.Realtime))
                    {
                        QueueFrame();
                    }
                }
                SetStatus(
                    ViewportPresentationState.Ready,
                    $"Presented scene revision {lease.TargetRevision}.");
            }
        }
        finally
        {
            if (releaseLease)
            {
                ReleaseNotSubmittedOrQuarantine(stream, lease, lifetime);
            }
        }
    }

    private static void ReleaseNotSubmittedOrQuarantine(
        StreamPresentationState stream,
        ViewportFrameLease lease,
        ViewportPresentationLifetime lifetime)
    {
        try
        {
            lease.Dispose();
        }
        catch
        {
            stream.IsQuarantined = true;
            lease.Quarantine();
            stream.ImportedSlots.TryGetValue(lease.SlotIdentity, out var imported);
            lifetime.QuarantineFrame(
                lease,
                imported?.Image,
                imported?.WaitSemaphore,
                imported?.SignalSemaphore);
            throw;
        }
    }

    private static async Task<NativeReadyWaitResult> WaitForReadyFrameAsync(
        ViewportRenderStream stream,
        CancellationToken cancellationToken = default)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var taken = stream.TryTakeReady();
            if (!taken.Succeeded || taken.HasFrame)
            {
                return new NativeReadyWaitResult(taken, null);
            }

            var snapshot = stream.Poll();
            if (snapshot.Lifecycle == ViewportRenderStreamLifecycle.Faulted ||
                (!snapshot.HasPendingLatest && !snapshot.HasReadyFrame &&
                 !snapshot.RenderExecuting))
            {
                return new NativeReadyWaitResult(taken, snapshot);
            }

            // Never capture AvaloniaSynchronizationContext for the 1 ms native-ready poll.
            await Task.Delay(1, cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task<CompositionCommitResult> PresentReadyFrameAsync(
        StreamPresentationState stream,
        ViewportFrameLease lease,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        if (!IsCurrent(generation) || !CanPresentFrame(lease, generation) ||
            !lifetime.TryBeginFrame(out var admission))
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }
        using (admission)
        {
            var imported = await GetOrImportSlotAsync(stream, lease);
            var accessTracker = new CompositionConsumerAccessTracker();
            try
            {
                return await CommitAsync(
                    lease.AllocationExtent,
                    stream.GeometryGeneration,
                    surface_ ?? throw new InvalidOperationException(
                        "Viewport composition surface is no longer available."),
                    publishesVisibleSurface: true,
                    imported.Image,
                    imported.WaitSemaphore,
                    imported.SignalSemaphore,
                    accessTracker,
                    // Exact extent is part of the presentation generation. A candidate replaced
                    // before the compositor callback must never update the shared surface.
                    () => IsCurrent(generation) &&
                          geometryState_.CurrentGeneration == stream.GeometryGeneration &&
                          ReferenceEquals(desiredStream_, stream) &&
                          CanPresentFrame(lease, generation),
                    () => TryMarkPresented(
                        lease,
                        generation,
                        stream.GeometryGeneration));
            }
            catch
            {
                if (accessTracker.State !=
                    CompositionConsumerAccessState.NotSubmittedToConsumer)
                {
                    stream.IsQuarantined = true;
                    lease.Quarantine();
                    lifetime.QuarantineFrame(
                        lease,
                        imported.Image,
                        imported.WaitSemaphore,
                        imported.SignalSemaphore);
                }
                throw;
            }
        }
    }

    private async Task PreparePresentationCoreAsync(
        PreparedPresentationOperation operation,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var targetExtent = operation.Handle.TargetExtent;
        var renderSize = new ViewportRenderSize(targetExtent, targetExtent);
        if (!operation.Session.TryPublishLatest(renderSize, out var request))
        {
            throw new InvalidOperationException(
                "The viewport session did not publish the prepared presentation request.");
        }
        testHooks_?.ObserveRequest(request);
        if (request.SceneRasterMode == ViewportSceneRasterMode.Wireframe &&
            !operation.Stream.Stream.SupportsWireframe)
        {
            throw new ViewportPresentationUnsupportedFeatureException(
                "Viewport wireframe is unavailable because the native stream device did not " +
                "enable fillModeNonSolid.");
        }

        var requestWasSubmitted = false;
        var backpressureStartedAt = Stopwatch.GetTimestamp();
        try
        {
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeStageAsyncCore(
                        ViewportCompositionControlTestPoint.BeforeNativeSubmit,
                        cancellationToken);
                }
                var submitted = operation.Stream.Stream.SubmitLatest(
                    request,
                    testHooks_?.DiagnosticOverlay ?? ViewportRenderDiagnosticOverlay.None);
                if (submitted.Succeeded)
                {
                    requestWasSubmitted = true;
                    Interlocked.Increment(ref candidateNativeSubmissions_);
                    if (testHooks_ is not null)
                    {
                        await testHooks_.BeforeStageAsyncCore(
                            ViewportCompositionControlTestPoint.AfterNativeSubmit,
                            cancellationToken);
                    }
                    break;
                }
                if (submitted.Failure!.Kind != ViewportFrameFailureKind.Backpressure)
                {
                    throw new InvalidOperationException(submitted.Failure.Message);
                }
                if (Stopwatch.GetElapsedTime(backpressureStartedAt) >=
                    TimeSpan.FromMilliseconds(250))
                {
                    throw new ViewportPresentationRecoverableException(
                        "Viewport presentation remained resource-backpressured for 250 ms.");
                }
                await Task.Delay(TimeSpan.FromMilliseconds(1), cancellationToken);
            }
        }
        catch
        {
            if (!requestWasSubmitted)
            {
                operation.Session.RetryPublishedFrame(request);
            }
            throw;
        }

        var wait = await WaitForReadyFrameAsync(operation.Stream.Stream, cancellationToken);
        if (!wait.Take.Succeeded)
        {
            throw new InvalidOperationException(wait.Take.Failure!.Message);
        }
        if (!wait.Take.HasFrame)
        {
            throw new InvalidOperationException(
                wait.Snapshot?.Lifecycle == ViewportRenderStreamLifecycle.Faulted
                    ? "The prepared presentation stream faulted before publishing a frame."
                    : "The prepared presentation stream closed before publishing a frame.");
        }

        var lease = wait.Take.Lease!;
        Interlocked.Increment(ref candidateLeasesAcquired_);
        operation.Stream.ExposedSlots.Add(lease.SlotIdentity);
        testHooks_?.ObserveLease(lease);
        var releaseLease = true;
        try
        {
            if (testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.AfterLeaseAcquired,
                    cancellationToken);
            }
            // Cancellation after TryTakeReady succeeds still owns a native lease. Keep the
            // cancellation check inside the release guard so the slot is completed or
            // quarantined on every path.
            cancellationToken.ThrowIfCancellationRequested();
            if (!CanPreparePresentationFrame(operation, lease))
            {
                throw new OperationCanceledException(
                    "The prepared presentation frame became stale before composition submission.",
                    cancellationToken);
            }

            operation.Frame = PresentationFrame(lease);
            operation.RenderSize = new ViewportRenderSize(
                lease.LogicalExtent,
                lease.AllocationExtent);
            var result = await PresentPreparedPresentationFrameAsync(
                operation,
                lease,
                cancellationToken);
            if (result != CompositionCommitResult.ConsumerAccessed)
            {
                throw new OperationCanceledException(
                    "The prepared presentation frame was not consumed by the compositor.",
                    cancellationToken);
            }

            try
            {
                lease.Release(ViewportFrameCompletionKind.ConsumerAccessed);
                releaseLease = false;
            }
            catch
            {
                operation.Stream.IsQuarantined = true;
                lease.Quarantine();
                operation.Stream.ImportedSlots.TryGetValue(
                    lease.SlotIdentity,
                    out var imported);
                operation.Lifetime.QuarantineFrame(
                    lease,
                    imported?.Image,
                    imported?.WaitSemaphore,
                    imported?.SignalSemaphore);
                releaseLease = false;
                throw;
            }
        }
        finally
        {
            if (releaseLease)
            {
                ReleaseNotSubmittedOrQuarantine(operation.Stream, lease, operation.Lifetime);
            }
        }
    }

    private async Task<CompositionCommitResult> PresentPreparedPresentationFrameAsync(
        PreparedPresentationOperation operation,
        ViewportFrameLease lease,
        CancellationToken cancellationToken)
    {
        if (!CanPreparePresentationFrame(operation, lease) ||
            !operation.Lifetime.TryBeginFrame(out var admission))
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }
        using (admission)
        {
            Interlocked.Increment(ref candidateImageImportAttempts_);
            if (testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.BeforeImageImport,
                    cancellationToken);
            }
            var imported = await GetOrImportSlotAsync(
                operation.Stream,
                lease,
                trackPreparedCandidate: true);
            var accessTracker = new CompositionConsumerAccessTracker();
            try
            {
                Interlocked.Increment(ref candidateSurfaceUpdateAttempts_);
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeStageAsyncCore(
                        ViewportCompositionControlTestPoint.BeforeSurfaceUpdate,
                        cancellationToken);
                }
                return await CommitAsync(
                    lease.AllocationExtent,
                    operation.Handle.Ticket.CandidateGeometryGeneration,
                    operation.Surface,
                    publishesVisibleSurface: false,
                    imported.Image,
                    imported.WaitSemaphore,
                    imported.SignalSemaphore,
                    accessTracker,
                    () => !cancellationToken.IsCancellationRequested &&
                          CanPreparePresentationFrame(operation, lease),
                    static () => false);
            }
            catch
            {
                if (accessTracker.State !=
                    CompositionConsumerAccessState.NotSubmittedToConsumer)
                {
                    operation.Stream.IsQuarantined = true;
                    lease.Quarantine();
                    operation.Lifetime.QuarantineFrame(
                        lease,
                        imported.Image,
                        imported.WaitSemaphore,
                        imported.SignalSemaphore);
                }
                throw;
            }
        }
    }

    private bool CanPreparePresentationFrame(
        PreparedPresentationOperation operation,
        ViewportFrameLease lease) =>
        ReferenceEquals(preparingPresentation_, operation) &&
        IsCurrent(operation.AttachmentGeneration) &&
        ReferenceEquals(Session, operation.Session) &&
        ReferenceEquals(Lifetime, operation.Lifetime) &&
        operation.Handle.Ticket.BaseGeometryGeneration == geometryState_.CurrentGeneration &&
        geometryState_.HasExactSurface &&
        lease.LogicalExtent == operation.Handle.TargetExtent &&
        lease.AllocationExtent == operation.Handle.TargetExtent &&
        CanPresentFrameContent(lease, operation.AttachmentGeneration);

    private async Task<ImportedSlot> GetOrImportSlotAsync(
        StreamPresentationState stream,
        ViewportFrameLease lease,
        bool trackPreparedCandidate = false)
    {
        if (stream.ImportedSlots.TryGetValue(lease.SlotIdentity, out var existing))
        {
            if (existing.Handles != lease.NativeHandles)
            {
                throw new InvalidOperationException(
                    "Native viewport changed handles for a persistent V7 slot.");
            }
            return existing;
        }
        var interop = interop_ ?? throw new InvalidOperationException(
            "Viewport compositor interop is no longer available.");
        ICompositionImportedGpuImage? image = null;
        ICompositionImportedGpuSemaphore? waitSemaphore = null;
        ICompositionImportedGpuSemaphore? signalSemaphore = null;
        try
        {
            image = interop.ImportImage(
                new PlatformHandle(lease.NativeHandles.Image, ImageHandleType),
                CreateImageProperties(lease));
            if (trackPreparedCandidate)
            {
                Interlocked.Increment(ref candidateImagesImported_);
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeStageAsyncCore(
                        ViewportCompositionControlTestPoint.AfterImageImported,
                        CancellationToken.None);
                }
            }
            waitSemaphore = interop.ImportSemaphore(
                new PlatformHandle(lease.NativeHandles.WaitSemaphore, SemaphoreHandleType));
            if (trackPreparedCandidate && testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.AfterWaitSemaphoreImported,
                    CancellationToken.None);
            }
            signalSemaphore = interop.ImportSemaphore(
                new PlatformHandle(lease.NativeHandles.SignalSemaphore, SemaphoreHandleType));
            if (trackPreparedCandidate && testHooks_ is not null)
            {
                await testHooks_.BeforeStageAsyncCore(
                    ViewportCompositionControlTestPoint.AfterSignalSemaphoreImported,
                    CancellationToken.None);
            }
            var imported = new ImportedSlot(
                lease.SlotIdentity,
                lease.NativeHandles,
                image,
                waitSemaphore,
                signalSemaphore);
            stream.ImportedSlots.Add(lease.SlotIdentity, imported);
            return imported;
        }
        catch
        {
            await DisposeImportedResourcesAsync(image, waitSemaphore, signalSemaphore);
            throw;
        }
    }

    private bool PromoteStream(StreamPresentationState stream)
    {
        if (!ReferenceEquals(desiredStream_, stream) || ReferenceEquals(activeStream_, stream))
        {
            return false;
        }
        var previous = activeStream_;
        activeStream_ = stream;
        if (previous is not null)
        {
            _ = BeginRetireStream(previous);
        }
        return true;
    }

    private Task BeginRetireStream(StreamPresentationState stream)
    {
        if (stream.WorkFence.RetirementTask is not null)
        {
            return stream.WorkFence.RetirementTask;
        }
        var retirement = stream.WorkFence.BeginRetirement(
            stream.Stream.RequestClose,
            () => ReleaseStreamResourcesAsync(stream));
        retiringStreamTasks_.Add(retirement);
        _ = RemoveRetiredTaskAsync(stream, retirement);
        return retirement;
    }

    private async Task RemoveRetiredTaskAsync(
        StreamPresentationState stream,
        Task retirement)
    {
        try
        {
            await retirement;
        }
        catch (Exception exception)
        {
            stream.IsQuarantined = true;
            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                if (isAttached_)
                {
                    SetDegraded(
                        ViewportPresentationState.RenderFailed,
                        $"Scene View stream retirement failed: {exception.Message}");
                }
            }, DispatcherPriority.Background);
        }
        finally
        {
            if (Dispatcher.UIThread.CheckAccess())
            {
                retiringStreamTasks_.Remove(retirement);
            }
            else
            {
                await Dispatcher.UIThread.InvokeAsync(
                    () => retiringStreamTasks_.Remove(retirement),
                    DispatcherPriority.Background);
            }
        }
    }

    private static async Task ReleaseStreamResourcesAsync(StreamPresentationState stream)
    {
        if (stream.IsQuarantined)
        {
            return;
        }
        foreach (var imported in stream.ImportedSlots.Values)
        {
            await DisposeImportedResourcesAsync(
                imported.Image,
                imported.WaitSemaphore,
                imported.SignalSemaphore);
        }
        stream.ImportedSlots.Clear();
        foreach (var nativeSlot in stream.ExposedSlots)
        {
            stream.Stream.ReleaseSlotImport(nativeSlot);
        }
        stream.ExposedSlots.Clear();

        var snapshot = await WaitForStreamClosedAsync(stream.Stream);
        if (snapshot.Lifecycle == ViewportRenderStreamLifecycle.Faulted)
        {
            throw new InvalidOperationException(
                "Native viewport stream faulted during retirement.");
        }
        stream.Stream.DestroyClosed();
    }

    private static async Task<ViewportRenderStreamSnapshot> WaitForStreamClosedAsync(
        ViewportRenderStream stream)
    {
        while (true)
        {
            var snapshot = stream.Poll();
            if (snapshot.Lifecycle is ViewportRenderStreamLifecycle.Closed or
                ViewportRenderStreamLifecycle.Faulted)
            {
                return snapshot;
            }

            // Candidate generation retirement must not enqueue millisecond continuations
            // onto Avalonia's dispatcher while the user is resizing a dock panel.
            await Task.Delay(1).ConfigureAwait(false);
        }
    }

    private async Task<CompositionCommitResult> CommitAsync(
        ViewportExtent extent,
        ulong geometryGeneration,
        CompositionDrawingSurface targetSurface,
        bool publishesVisibleSurface,
        ICompositionImportedGpuImage image,
        ICompositionImportedGpuSemaphore waitSemaphore,
        ICompositionImportedGpuSemaphore signalSemaphore,
        CompositionConsumerAccessTracker accessTracker,
        Func<bool> canPresent,
        Func<bool> tryMarkPresented)
    {
        var visual = compositionVisual_;
        if (visual is null || !canPresent())
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }

        if (!publishesVisibleSurface)
        {
            // A prepared candidate is not attached to the visual tree. The Avalonia API already
            // schedules its server job and a compositor commit, so routing it through an extra
            // RequestCompositionUpdate callback only adds a layout/composition turn. Keep visible
            // surface publication on the exact commit boundary below, but stage the private
            // candidate immediately.
            return await CommitStagedSurfaceAsync(
                targetSurface,
                image,
                waitSemaphore,
                signalSemaphore,
                accessTracker,
                canPresent);
        }

        var commit = new PendingCompositionCommit(
            extent,
            geometryGeneration,
            targetSurface,
            publishesVisibleSurface,
            image,
            waitSemaphore,
            signalSemaphore,
            accessTracker,
            canPresent,
            tryMarkPresented);
        QueueCompositionCommit(commit, visual);

        var completion = await commit.Completion.Task;
        if (completion == CompositionUpdateCompletion.NotSubmittedToConsumer)
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }
        if (completion == CompositionUpdateCompletion.ConsumerAccessed)
        {
            return CompositionCommitResult.ConsumerAccessed;
        }
        var presented = Dispatcher.UIThread.CheckAccess()
            ? CompositionUpdatePresentationPolicy.CanMarkPresented(
                  completion,
                  commit.CanPresent()) && commit.TryMarkPresented()
            : await Dispatcher.UIThread.InvokeAsync(
                () => CompositionUpdatePresentationPolicy.CanMarkPresented(
                          completion,
                          commit.CanPresent()) && commit.TryMarkPresented(),
                DispatcherPriority.Render);
        return presented
            ? CompositionCommitResult.Presented
            : CompositionCommitResult.ConsumerAccessed;
    }

    private async Task<CompositionCommitResult> CommitStagedSurfaceAsync(
        CompositionDrawingSurface targetSurface,
        ICompositionImportedGpuImage image,
        ICompositionImportedGpuSemaphore waitSemaphore,
        ICompositionImportedGpuSemaphore signalSemaphore,
        CompositionConsumerAccessTracker accessTracker,
        Func<bool> canPresent)
    {
        if (!canPresent())
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }

        var update = targetSurface.UpdateWithSemaphoresAsync(
            image,
            waitSemaphore,
            signalSemaphore);
        accessTracker.MarkSubmissionStarted();
        try
        {
            testHooks_?.AtSynchronousStageCore(
                ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted);
        }
        catch
        {
            _ = ObserveAmbiguousStagedSurfaceUpdateAsync(update, accessTracker);
            throw;
        }

        await update.ConfigureAwait(false);
        accessTracker.MarkConsumerAccessed();
        return CompositionCommitResult.ConsumerAccessed;
    }

    private static async Task ObserveAmbiguousStagedSurfaceUpdateAsync(
        Task update,
        CompositionConsumerAccessTracker accessTracker)
    {
        try
        {
            await update.ConfigureAwait(false);
            accessTracker.MarkConsumerAccessed();
        }
        catch (Exception exception)
        {
            Trace.TraceError(
                "An ambiguous staged viewport surface update faulted: {0}",
                exception);
        }
    }

    private void QueueCompositionCommit(
        PendingCompositionCommit commit,
        CompositionSurfaceVisual visual)
    {
        // Native production is latest-wins, and the composition boundary must be too. Avalonia's
        // swapchain sample performs one Present per compositor callback; submitting several
        // surface snapshots in the same callback cycle can strand release semaphores.
        pendingCompositionCommit_?.Completion.TrySetResult(
            CompositionUpdateCompletion.NotSubmittedToConsumer);
        pendingCompositionCommit_ = commit;
        if (isCompositionCommitQueued_)
        {
            return;
        }

        isCompositionCommitQueued_ = true;
        var ticket = ++compositionCommitTicket_;
        visual.Compositor.RequestCompositionUpdate(
            () => PublishCompositionCommit(ticket, visual));
    }

    private void PublishCompositionCommit(
        ulong ticket,
        CompositionSurfaceVisual visual)
    {
        if (ticket != compositionCommitTicket_)
        {
            return;
        }

        isCompositionCommitQueued_ = false;
        var commit = pendingCompositionCommit_;
        pendingCompositionCommit_ = null;
        if (commit is null)
        {
            return;
        }
        if (!ReferenceEquals(compositionVisual_, visual) ||
            (commit.PublishesVisibleSurface &&
             !ReferenceEquals(surface_, commit.TargetSurface)))
        {
            commit.Completion.TrySetResult(
                CompositionUpdateCompletion.NotSubmittedToConsumer);
            return;
        }
        if (!commit.CanPresent())
        {
            if (commit.GeometryGeneration != geometryState_.CurrentGeneration ||
                !TryGetRenderSize(out var currentSize) ||
                currentSize.LogicalExtent != commit.Extent)
            {
                rejectedNonExactCandidates_++;
            }
            commit.Completion.TrySetResult(
                CompositionUpdateCompletion.NotSubmittedToConsumer);
            return;
        }

        Task update;
        try
        {
            update = commit.TargetSurface.UpdateWithSemaphoresAsync(
                commit.Image,
                commit.WaitSemaphore,
                commit.SignalSemaphore);
            commit.AccessTracker.MarkSubmissionStarted();
            try
            {
                if (!commit.PublishesVisibleSurface)
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted);
                }
            }
            catch (Exception exception)
            {
                // The external consumer may already own the semaphores. Publish the injected
                // ambiguity first, but continue observing Avalonia without guessing ownership.
                commit.Completion.TrySetException(exception);
                _ = CompleteSurfaceUpdateAsync(update, commit, visual);
                return;
            }
        }
        catch (Exception exception)
        {
            commit.Completion.TrySetException(exception);
            return;
        }
        _ = CompleteSurfaceUpdateAsync(update, commit, visual);
    }

    private async Task CompleteSurfaceUpdateAsync(
        Task update,
        PendingCompositionCommit commit,
        CompositionSurfaceVisual visual)
    {
        try
        {
            await update.ConfigureAwait(false);
            commit.AccessTracker.MarkConsumerAccessed();
            await Dispatcher.UIThread.InvokeAsync(
                () => CompleteSurfaceUpdateOnUiThread(commit, visual),
                DispatcherPriority.Render);
        }
        catch (Exception exception)
        {
            commit.Completion.TrySetException(exception);
        }
    }

    private void CompleteSurfaceUpdateOnUiThread(
        PendingCompositionCommit commit,
        CompositionSurfaceVisual visual)
    {
        var completion = CompositionUpdateCompletion.ConsumerAccessed;
        if (commit.PublishesVisibleSurface && commit.CanPresent() &&
            ReferenceEquals(compositionVisual_, visual) &&
            ReferenceEquals(surface_, commit.TargetSurface))
        {
            geometryState_.MarkSurfaceUpdate(commit.Extent, commit.GeometryGeneration);
            geometryDiagnostics_.MarkExactSurfaceSubmitted(
                commit.GeometryGeneration,
                Stopwatch.GetTimestamp());
            UpdateVisualPlacement();
            completion = CompositionUpdateCompletion.VisibleSurfacePublished;
        }
        commit.Completion.TrySetResult(completion);
    }

    private static async Task DisposeImportedResourcesAsync(
        ICompositionImportedGpuImage? image,
        ICompositionImportedGpuSemaphore? waitSemaphore,
        ICompositionImportedGpuSemaphore? signalSemaphore)
    {
        var failures = (await Task.WhenAll(
                DisposeAsync(image),
                DisposeAsync(waitSemaphore),
                DisposeAsync(signalSemaphore)))
            .Where(static failure => failure is not null)
            .Cast<Exception>()
            .ToArray();
        if (failures.Length == 1)
        {
            ExceptionDispatchInfo.Capture(failures[0]).Throw();
        }
        if (failures.Length > 1)
        {
            throw new AggregateException(
                "Viewport imported resources did not release cleanly.",
                failures);
        }

        static async Task<Exception?> DisposeAsync(ICompositionGpuImportedObject? resource)
        {
            if (resource is null)
            {
                return null;
            }
            try
            {
                await resource.DisposeAsync();
                return null;
            }
            catch (Exception exception)
            {
                return exception;
            }
        }
    }

    private bool IsCurrent(ulong generation) => isAttached_ && generation_ == generation;

    private bool CanPresentFrame(ViewportFrameLease lease, ulong generation)
        => !presentationPreparation_.HasPreparation &&
           CanPresentFrameContent(lease, generation) &&
           CanPresentAtCurrentGeometry(lease);

    private bool CanPresentFrameContent(ViewportFrameLease lease, ulong generation)
    {
        if (!IsCurrent(generation) || !IsEffectivelyVisible || Session is not { } session ||
            !session.CanPresentPublishedFrame(lease.RequestSequence, lease.TargetRevision))
        {
            return false;
        }
        return presentationState_.CanPresent(
            generation,
            PresentationFrame(lease),
            session.Current);
    }

    private bool TryMarkPresented(
        ViewportFrameLease lease,
        ulong generation,
        ulong geometryGeneration)
    {
        if (!IsCurrent(generation) || Session is not { } session ||
            !session.MarkPublishedFramePresented(lease.RequestSequence, lease.TargetRevision))
        {
            return false;
        }
        var presented = presentationState_.TryMarkPresented(
            generation,
            PresentationFrame(lease),
            session.Current);
        if (presented)
        {
            lastPresentedSize_ = new ViewportRenderSize(
                lease.LogicalExtent,
                lease.AllocationExtent);
            // PublishCompositionCommit admitted this frame only while the panel had this exact
            // extent. Consumer completion may arrive after Bounds has already advanced again.
            lastPresentedPanelExtent_ = lease.LogicalExtent;
            lastPresentedFrame_ = PresentationFrame(lease);
            exactExtentPresentedFrames_++;
            var presentedAt = Stopwatch.GetTimestamp();
            cadenceTracker_.Record(presentedAt);
            geometryDiagnostics_.MarkExactSurfaceCompleted(
                geometryGeneration,
                presentedAt);
        }
        return presented;
    }

    private bool CanPresentAtCurrentGeometry(ViewportFrameLease lease) =>
        TryGetRenderSize(out var currentSize) &&
        ViewportResizePresentationPolicy.CanPresentCompletedFrame(
            new ViewportRenderSize(lease.LogicalExtent, lease.AllocationExtent),
            currentSize);

    private static ViewportPresentationFrame PresentationFrame(ViewportFrameLease lease) => new(
        lease.SessionId,
        lease.TargetKind,
        lease.TargetId,
        lease.TargetRevision,
        lease.RequestSequence);

    private bool TryGetRenderSize(out ViewportRenderSize renderSize)
    {
        renderSize = default;
        var scaling = topLevel_?.RenderScaling ?? 0;
        if (!ViewportPhysicalExtentPolicy.TryCalculate(
                Bounds.Width,
                Bounds.Height,
                scaling,
                out var logicalExtent))
        {
            return false;
        }

        renderSize = new ViewportRenderSize(logicalExtent, logicalExtent);
        return true;
    }

    private void UpdateVisualPlacement()
    {
        if (compositionVisual_ is not { } visual)
        {
            return;
        }
        visual.Offset = Vector3.Zero;
        var scaling = topLevel_?.RenderScaling ?? 0;
        var hasExactSurface = TryGetRenderSize(out var currentSize) &&
                              geometryState_.CurrentExtent == currentSize.LogicalExtent &&
                              geometryState_.HasExactSurface;
        // Never expose an old-size image through a new panel rectangle. Bounds and this opacity
        // mutation enter the same Avalonia commit; the exact-size surface update reenables the
        // visual in its own commit below.
        SetRequestedVisualOpacity(visual, hasExactSurface ? 1 : 0);
        if (scaling > 0 &&
            geometryState_.SurfaceExtent.Width != 0 &&
            geometryState_.SurfaceExtent.Height != 0)
        {
            visual.Size = new Vector(
                geometryState_.SurfaceExtent.Width / scaling,
                geometryState_.SurfaceExtent.Height / scaling);
            return;
        }
        visual.Size = new Vector(
            (float)Math.Max(0, Bounds.Width),
            (float)Math.Max(0, Bounds.Height));
    }

    internal bool TryCapturePresentationLayoutProbe(
        ulong ticket,
        out ViewportExtent extent)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (!presentationPreparation_.IsLayoutProbeActive ||
            !TryGetRenderSize(out var renderSize))
        {
            extent = default;
            return false;
        }
        presentationPreparation_.ObserveLayoutProbe(renderSize.LogicalExtent);
        return presentationPreparation_.TryGetLayoutProbeExtent(ticket, out extent);
    }

    internal void EndPresentationLayoutProbe(ulong ticket)
    {
        Dispatcher.UIThread.VerifyAccess();
        presentationPreparation_.EndLayoutProbe(ticket);
    }

    private bool TryHandlePresentationLayoutChange(ViewportGeometryChangeSource source)
    {
        var extent = TryGetRenderSize(out var renderSize)
            ? renderSize.LogicalExtent
            : default;
        var disposition = presentationPreparation_.ObserveBounds(extent, out _);
        return disposition != ViewportPresentationLayoutDisposition.None;
    }

    internal object? PresentationAtomicScope
    {
        get
        {
            Dispatcher.UIThread.VerifyAccess();
            return compositionVisual_?.Compositor;
        }
    }

    internal ViewportPresentationTelemetryIdentity CreatePresentationTelemetryIdentity(
        ViewportPresentationTransactionId transactionId,
        ulong geometryGeneration,
        ViewportExtent extent)
    {
        Dispatcher.UIThread.VerifyAccess();
        var session = Session?.Current ?? throw new InvalidOperationException(
            "The viewport presentation endpoint has no session identity.");
        return new ViewportPresentationTelemetryIdentity(
            endpointId_,
            session.SessionId,
            generation_,
            transactionId,
            geometryGeneration,
            extent);
    }

    internal ulong NextPresentationGeometryGeneration
    {
        get
        {
            Dispatcher.UIThread.VerifyAccess();
            return checked(geometryState_.CurrentGeneration + 1);
        }
    }

    internal bool TryValidatePreparedPresentation(ViewportPreparedPresentation prepared)
    {
        ArgumentNullException.ThrowIfNull(prepared);
        Dispatcher.UIThread.VerifyAccess();
        return !(testHooks_?.ShouldRejectPreparedValidation() ?? false) &&
               ReferenceEquals(prepared.Owner, this) &&
               preparedPresentation_ is { } operation &&
               ReferenceEquals(operation.Handle, prepared) &&
               !operation.Cancellation.IsCancellationRequested &&
               isAttached_ && IsEffectivelyVisible &&
               interop_ is { IsLost: false } &&
               operation.Lifetime.IsAcceptingFrames &&
               compositionVisual_ is not null && surface_ is not null &&
               topLevel_?.RenderScaling is > 0 &&
               presentationPreparation_.IsArmedExtentExact(prepared.Ticket) &&
               IsPreparedPresentationCurrent(operation);
    }

    internal PresentationPublishReceipt ApplyPreparedPresentation(
        ViewportPreparedPresentation prepared,
        ViewportGeometryChangeSource source)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (!TryValidatePreparedPresentation(prepared) ||
            preparedPresentation_ is not { } operation ||
            compositionVisual_ is not { } visual || surface_ is not { } oldSurface ||
            topLevel_?.RenderScaling is not > 0)
        {
            throw new InvalidOperationException(
                "The prepared viewport presentation is no longer publishable.");
        }

        var replacedStreams = DistinctStreams(activeStream_, desiredStream_)
            .Where(stream => !ReferenceEquals(stream, operation.Stream))
            .ToArray();
        var receipt = new PresentationPublishReceipt(
            operation,
            visual,
            oldSurface,
            oldSurface,
            visual.Size,
            visual.Opacity,
            source,
            replacedStreams);
        var targetSize = new Vector(
            operation.Handle.TargetExtent.Width / topLevel_.RenderScaling,
            operation.Handle.TargetExtent.Height / topLevel_.RenderScaling);
        try
        {
            ViewportPresentationVisualMutation.ApplyStrong(
                CreatePresentationVisualMutationSteps(receipt, targetSize));
        }
        catch (ViewportPresentationVisualMutationAmbiguousException exception)
        {
            QuarantinePublishedPresentation(receipt, exception.Message);
            throw new PublicationOutcomeAmbiguousException(receipt, exception);
        }
        return receipt;
    }

    internal void RollbackPreparedPresentation(PresentationPublishReceipt receipt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        Dispatcher.UIThread.VerifyAccess();
        if (receipt.IsFinalized || receipt.IsRolledBack)
        {
            return;
        }
        try
        {
            ViewportPresentationVisualMutation.RestoreStrong(
                CreatePresentationVisualMutationSteps(receipt, receipt.Visual.Size));
        }
        catch (ViewportPresentationVisualMutationAmbiguousException exception)
        {
            QuarantinePublishedPresentation(receipt, exception.Message);
            throw new PublicationOutcomeAmbiguousException(receipt, exception);
        }
        receipt.IsRolledBack = true;
    }

    private IReadOnlyList<ViewportPresentationVisualMutationStep>
        CreatePresentationVisualMutationSteps(
            PresentationPublishReceipt receipt,
            Vector targetSize) =>
        [
            new ViewportPresentationVisualMutationStep(
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeApplySurface);
                    receipt.Visual.Surface = receipt.Operation.Surface;
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.AfterApplySurface);
                },
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeRestoreSurface);
                    receipt.Visual.Surface = receipt.OldVisualSurface;
                }),
            new ViewportPresentationVisualMutationStep(
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeApplySize);
                    receipt.Visual.Size = targetSize;
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.AfterApplySize);
                },
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeRestoreSize);
                    receipt.Visual.Size = receipt.OldVisualSize;
                }),
            new ViewportPresentationVisualMutationStep(
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeApplyOpacity);
                    SetRequestedVisualOpacity(receipt.Visual, 1);
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.AfterApplyOpacity);
                },
                () =>
                {
                    testHooks_?.AtSynchronousStageCore(
                        ViewportCompositionControlTestPoint.BeforeRestoreOpacity);
                    SetRequestedVisualOpacity(receipt.Visual, receipt.OldVisualOpacity);
                }),
        ];

    internal Task FinalizePreparedPresentation(
        PresentationPublishReceipt receipt,
        Task switchRendered)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        ArgumentNullException.ThrowIfNull(switchRendered);
        Dispatcher.UIThread.VerifyAccess();
        var operation = receipt.Operation;
        if (receipt.IsFinalized || receipt.IsRolledBack ||
            !TryValidatePreparedPresentation(operation.Handle) ||
            !operation.Session.MarkPublishedFramePresented(
                operation.Frame.Sequence,
                operation.Frame.TargetRevision) ||
            !presentationState_.TryMarkPresented(
                operation.AttachmentGeneration,
                operation.Frame,
                operation.Session.Current) ||
            !presentationPreparation_.TryCompleteArmed(operation.Handle.Ticket))
        {
            throw new InvalidOperationException(
                "The validated viewport presentation could not be finalized.");
        }

        if (!geometryState_.Synchronize(operation.Handle.TargetExtent) ||
            geometryState_.CurrentGeneration !=
                operation.Handle.Ticket.CandidateGeometryGeneration)
        {
            throw new InvalidOperationException(
                "The viewport presentation did not advance the expected geometry generation.");
        }

        var committedAt = Stopwatch.GetTimestamp();
        geometryDiagnostics_.RecordGeneration(
            geometryState_.CurrentGeneration,
            operation.Handle.TargetExtent,
            receipt.Source,
            committedAt);
        geometryState_.MarkSurfaceUpdate(
            operation.Handle.TargetExtent,
            geometryState_.CurrentGeneration);
        geometryDiagnostics_.MarkExactSurfaceSubmitted(
            geometryState_.CurrentGeneration,
            committedAt);

        pendingCompositionCommit_?.Completion.TrySetResult(
            CompositionUpdateCompletion.NotSubmittedToConsumer);
        pendingCompositionCommit_ = null;
        isCompositionCommitQueued_ = false;
        compositionCommitTicket_++;

        surface_ = operation.Surface;
        operation.OwnsSurface = false;
        activeStream_ = operation.Stream;
        desiredStream_ = operation.Stream;
        operation.OwnsStream = false;

        lastPresentedSize_ = operation.RenderSize;
        lastPresentedPanelExtent_ = operation.Handle.TargetExtent;
        lastPresentedFrame_ = operation.Frame;
        preparedPresentation_ = null;
        operation.DisposeCancellation();
        receipt.IsFinalized = true;
        var retirement = QueueReplacedFrontRetirement(
            receipt.OldSurface,
            receipt.ReplacedStreams,
            switchRendered);

        SetStatus(
            ViewportPresentationState.Ready,
            $"Published scene revision {operation.Frame.TargetRevision}; awaiting compositor render.");
        if (CanScheduleAutomaticRealtime() &&
            TryInvalidateOpenSession(operation.Session, ViewportInvalidationReason.Realtime))
        {
            QueueFrame();
        }
        return retirement;
    }

    internal void MarkPreparedPresentationRendered(
        PresentationPublishReceipt receipt,
        long renderedAt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        Dispatcher.UIThread.VerifyAccess();
        if (!receipt.IsFinalized || receipt.IsRolledBack || receipt.IsRendered)
        {
            return;
        }

        receipt.IsRendered = true;
        exactExtentPresentedFrames_++;
        cadenceTracker_.Record(renderedAt);
        geometryDiagnostics_.MarkExactSurfaceCompleted(
            receipt.Operation.Handle.Ticket.CandidateGeometryGeneration,
            renderedAt);
        receipt.Operation.Handle.Complete(ViewportPresentationTransactionResult.Committed);
        if (ReferenceEquals(surface_, receipt.Operation.Surface))
        {
            SetStatus(
                ViewportPresentationState.Ready,
                $"Rendered scene revision {receipt.Operation.Frame.TargetRevision}.");
        }
    }

    internal Task RequestPresentationBatchRendered()
    {
        Dispatcher.UIThread.VerifyAccess();
        var visual = compositionVisual_ ?? throw new InvalidOperationException(
            "The viewport presentation endpoint is detached.");
        return visual.Compositor.RequestCompositionBatchCommitAsync().Rendered;
    }

    internal void QuarantinePublishedPresentation(
        PresentationPublishReceipt receipt,
        string reason)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        Dispatcher.UIThread.VerifyAccess();
        var operation = receipt.Operation;
        operation.RequestCancellation();
        receipt.IsQuarantined = true;
        receipt.QuarantineReason ??= reason;
        operation.Stream.IsQuarantined = true;
        foreach (var stream in receipt.ReplacedStreams.Append(operation.Stream))
        {
            stream.IsQuarantined = true;
            if (!quarantinedStreams_.Contains(stream))
            {
                quarantinedStreams_.Add(stream);
            }
        }
        foreach (var surface in new[] { receipt.OldSurface, operation.Surface })
        {
            if (!quarantinedSurfaces_.Contains(surface))
            {
                quarantinedSurfaces_.Add(surface);
            }
        }
        if (!quarantinedPresentations_.Contains(receipt))
        {
            quarantinedPresentations_.Add(receipt);
        }
        if (ReferenceEquals(preparedPresentation_, operation))
        {
            presentationPreparation_.TryCancel(operation.Handle.Ticket);
            preparedPresentation_ = null;
        }
        operation.OwnsSurface = false;
        operation.OwnsStream = false;
        preparingPresentation_?.RequestCancellation();
        isFrameQueued_ = false;
        queuedFrameUsesEarlyAdmission_ = false;
        queuedFrameTicket_++;
        pendingCompositionCommit_?.Completion.TrySetResult(
            CompositionUpdateCompletion.NotSubmittedToConsumer);
        pendingCompositionCommit_ = null;
        isCompositionCommitQueued_ = false;
        compositionCommitTicket_++;
        if (activeStream_ is not null && quarantinedStreams_.Contains(activeStream_))
        {
            activeStream_ = null;
        }
        if (desiredStream_ is not null && quarantinedStreams_.Contains(desiredStream_))
        {
            desiredStream_ = null;
        }
        TransferQuarantinedPresentationOwnership();
        operation.Handle.Complete(ViewportPresentationTransactionResult.Quarantined);
        SetDegraded(
            ViewportPresentationState.RenderFailed,
            $"Viewport presentation outcome is ambiguous and its resources were quarantined: {reason}");
    }

    private void TransferQuarantinedPresentationOwnership()
    {
        foreach (var receipt in quarantinedPresentations_)
        {
            if (receipt.QuarantineTransferReceipt is not null)
            {
                continue;
            }

            var streams = receipt.ReplacedStreams
                .Append(receipt.Operation.Stream)
                .Distinct()
                .Cast<object>()
                .ToArray();
            var surfaces = new[] { receipt.OldSurface, receipt.Operation.Surface }
                .Distinct()
                .Cast<object>()
                .ToArray();
            receipt.QuarantineTransferReceipt =
                receipt.Operation.Lifetime.ProcessQuarantineRegistry.TransferPublished(
                    endpointId_.Value,
                    receipt.Operation,
                    streams,
                    surfaces,
                    receipt.QuarantineReason ??
                    "The viewport publication outcome was ambiguous.");
        }
    }

    private void SetRequestedVisualOpacity(
        CompositionSurfaceVisual visual,
        float opacity)
    {
        visual.Opacity = opacity;
        if (ReferenceEquals(compositionVisual_, visual))
        {
            geometryDiagnostics_.MarkRequestedVisualHidden(
                opacity <= 0,
                Stopwatch.GetTimestamp());
        }
    }

    private bool IsPreparedPresentationCurrent(PreparedPresentationOperation operation) =>
        !operation.Cancellation.IsCancellationRequested &&
        isAttached_ && IsEffectivelyVisible &&
        IsCurrent(operation.AttachmentGeneration) &&
        ReferenceEquals(Session, operation.Session) &&
        ReferenceEquals(Lifetime, operation.Lifetime) &&
        operation.Lifetime.IsAcceptingFrames &&
        operation.Handle.Ticket.BaseGeometryGeneration == geometryState_.CurrentGeneration &&
        geometryState_.HasExactSurface &&
        operation.RenderSize.LogicalExtent == operation.Handle.TargetExtent &&
        operation.RenderSize.AllocationExtent == operation.Handle.TargetExtent &&
        operation.Session.CanPresentPublishedFrame(
            operation.Frame.Sequence,
            operation.Frame.TargetRevision) &&
        presentationState_.CanPresent(
            operation.AttachmentGeneration,
            operation.Frame,
            operation.Session.Current);

    private void QueueUncommittedPresentationRetirement(
        PreparedPresentationOperation operation)
    {
        operation.RequestCancellation();
        var stream = operation.OwnsStream ? operation.Stream : null;
        var surface = operation.OwnsSurface ? operation.Surface : null;
        operation.OwnsStream = false;
        operation.OwnsSurface = false;
        if (stream is null && surface is null)
        {
            operation.DisposeCancellation();
            return;
        }

        var retirement = RetireUncommittedPresentationAsync(operation, stream, surface);
        TrackRetiringSurfaceTask(retirement);
    }

    private async Task RetireUncommittedPresentationAsync(
        PreparedPresentationOperation operation,
        StreamPresentationState? stream,
        CompositionDrawingSurface? surface)
    {
        try
        {
            if (stream is not null)
            {
                await BeginRetireStream(stream);
            }
        }
        finally
        {
            await DisposeSurfaceOnUiThreadAsync(surface);
            operation.DisposeCancellation();
            Interlocked.Increment(ref candidateCleanupCompletions_);
        }
    }

    private async Task RetireAcquiredPresentationAsync(
        StreamPresentationState stream,
        CompositionDrawingSurface? surface,
        CancellationTokenSource? cancellation)
    {
        try
        {
            await BeginRetireStream(stream);
        }
        finally
        {
            await DisposeSurfaceOnUiThreadAsync(surface);
            cancellation?.Dispose();
            Interlocked.Increment(ref candidateCleanupCompletions_);
        }
    }

    private Task QueueReplacedFrontRetirement(
        CompositionDrawingSurface oldSurface,
        IReadOnlyList<StreamPresentationState> oldStreams,
        Task switchRendered)
    {
        var retirement = RetireReplacedFrontAsync(oldSurface, oldStreams, switchRendered);
        if (testHooks_ is not null)
        {
            retirement = testHooks_.WrapReplacedFrontRetirementTask(retirement);
        }
        TrackRetiringSurfaceTask(retirement);
        return retirement;
    }

    private async Task RetireReplacedFrontAsync(
        CompositionDrawingSurface oldSurface,
        IReadOnlyList<StreamPresentationState> oldStreams,
        Task switchRendered)
    {
        await switchRendered;
        Task[] streamRetirements;
        if (Dispatcher.UIThread.CheckAccess())
        {
            streamRetirements = oldStreams.Select(BeginRetireStream).ToArray();
        }
        else
        {
            streamRetirements = await Dispatcher.UIThread.InvokeAsync(
                () => oldStreams.Select(BeginRetireStream).ToArray(),
                DispatcherPriority.Render);
        }
        await Task.WhenAll(streamRetirements);
        if (testHooks_ is not null)
        {
            await testHooks_.BeforeStageAsyncCore(
                ViewportCompositionControlTestPoint.BeforeOldSurfaceDispose,
                CancellationToken.None);
        }
        await DisposeSurfaceOnUiThreadAsync(oldSurface);
    }

    private void TrackRetiringSurfaceTask(Task retirement)
    {
        retiringSurfaceTasks_.Add(retirement);
        _ = RemoveRetiringSurfaceTaskAsync(retirement);
    }

    private async Task RemoveRetiringSurfaceTaskAsync(Task retirement)
    {
        try
        {
            await retirement;
        }
        catch (Exception exception)
        {
            if (isAttached_)
            {
                await Dispatcher.UIThread.InvokeAsync(
                    () => SetDegraded(
                        ViewportPresentationState.RenderFailed,
                        $"Scene View surface retirement failed: {exception.Message}"),
                    DispatcherPriority.Background);
            }
        }
        finally
        {
            if (Dispatcher.UIThread.CheckAccess())
            {
                retiringSurfaceTasks_.Remove(retirement);
            }
            else
            {
                await Dispatcher.UIThread.InvokeAsync(
                    () => retiringSurfaceTasks_.Remove(retirement),
                    DispatcherPriority.Background);
            }
        }
    }

    private static async Task DisposeSurfaceOnUiThreadAsync(
        CompositionDrawingSurface? surface)
    {
        if (surface is null)
        {
            return;
        }
        if (Dispatcher.UIThread.CheckAccess())
        {
            surface.Dispose();
            return;
        }
        await Dispatcher.UIThread.InvokeAsync(surface.Dispose, DispatcherPriority.Send);
    }

    private void ResumeFrontPresentation()
    {
        Dispatcher.UIThread.Post(
            () =>
            {
                if (!isAttached_ || presentationPreparation_.HasPreparation)
                {
                    return;
                }
                var geometryChanged = SynchronizeGeometryGeneration(
                    ViewportGeometryChangeSource.Bounds);
                UpdateVisualPlacement();
                if (Session is { } session)
                {
                    _ = TryInvalidateOpenSession(
                        session,
                        ViewportInvalidationReason.ExtentChanged);
                }
                InvalidatePresentation(
                    resetPresentationEpoch: false,
                    preferEarlyAdmission: geometryChanged);
            },
            DispatcherPriority.Render);
    }

    private void OnScalingChanged(object? sender, EventArgs e)
    {
        if (ReferenceEquals(sender, topLevel_))
        {
            if (TryHandlePresentationLayoutChange(ViewportGeometryChangeSource.Scaling))
            {
                return;
            }
            var geometryChanged = SynchronizeGeometryGeneration(
                ViewportGeometryChangeSource.Scaling);
            UpdateVisualPlacement();
            InvalidatePresentation(
                resetPresentationEpoch: false,
                preferEarlyAdmission: geometryChanged);
        }
    }

    private bool SynchronizeGeometryGeneration(ViewportGeometryChangeSource source)
    {
        if (!TryGetRenderSize(out var currentSize))
        {
            if (geometryState_.CurrentExtent.Width != 0 || activeStream_ is not null ||
                desiredStream_ is not null)
            {
                geometryState_.Invalidate();
                geometryDiagnostics_.RecordGeneration(
                    geometryState_.CurrentGeneration,
                    default,
                    source,
                    Stopwatch.GetTimestamp());
                RetireCurrentStreams();
                // Preserve a dirty extent across a collapsed/zero-size interval. If the panel
                // later returns to the same pixel size, ViewportSession's last render size alone
                // would otherwise make an OnDemand session look clean while its surface is hidden.
                if (isAttached_ && Session is { } session)
                {
                    _ = TryInvalidateOpenSession(
                        session,
                        ViewportInvalidationReason.ExtentChanged);
                }
                return true;
            }
            return false;
        }
        if (geometryState_.Synchronize(currentSize.LogicalExtent))
        {
            geometryDiagnostics_.RecordGeneration(
                geometryState_.CurrentGeneration,
                currentSize.LogicalExtent,
                source,
                Stopwatch.GetTimestamp());
            RetireCurrentStreams();
            return true;
        }
        return false;
    }

    private void RetireCurrentStreams()
    {
        var replacedStreams = DistinctStreams(activeStream_, desiredStream_).ToArray();
        activeStream_ = null;
        desiredStream_ = null;
        foreach (var replacedStream in replacedStreams)
        {
            _ = BeginRetireStream(replacedStream);
        }
    }

    private bool CanScheduleAutomaticRealtime() =>
        !presentationPreparation_.HasPreparation &&
        ViewportRealtimeAdmissionPolicy.ShouldInvalidate(
            IsRealtime,
            desiredStream_ is not null,
            desiredStream_ is not null && ReferenceEquals(activeStream_, desiredStream_));

    private void SynchronizeLifetimeSubscription()
    {
        var lifetime = isAttached_ ? Lifetime : null;
        if (ReferenceEquals(subscribedLifetime_, lifetime))
        {
            return;
        }
        if (subscribedLifetime_ is not null)
        {
            subscribedLifetime_.Resumed -= OnPresentationLifetimeResumed;
        }
        subscribedLifetime_ = lifetime;
        if (subscribedLifetime_ is not null)
        {
            subscribedLifetime_.Resumed += OnPresentationLifetimeResumed;
        }
    }

    private void SynchronizeSessionSubscription()
    {
        var session = isAttached_ ? Session : null;
        if (ReferenceEquals(subscribedSession_, session))
        {
            return;
        }
        if (subscribedSession_ is not null)
        {
            subscribedSession_.RefreshRequested -= OnSessionRefreshRequested;
        }
        subscribedSession_ = session;
        if (subscribedSession_ is not null)
        {
            subscribedSession_.RefreshRequested += OnSessionRefreshRequested;
        }
    }

    private void OnSessionRefreshRequested(object? sender, EventArgs e)
    {
        if (!ReferenceEquals(sender, subscribedSession_))
        {
            return;
        }
        if (Dispatcher.UIThread.CheckAccess())
        {
            InvalidatePresentation(resetPresentationEpoch: false);
            return;
        }
        Dispatcher.UIThread.Post(
            () =>
            {
                if (ReferenceEquals(sender, subscribedSession_))
                {
                    InvalidatePresentation(resetPresentationEpoch: false);
                }
            },
            DispatcherPriority.Render);
    }

    private void SynchronizeVisibilitySubscriptions()
    {
        ClearVisibilitySubscriptions();
        if (!isAttached_)
        {
            return;
        }

        visibilitySources_.Add(this);
        visibilitySources_.AddRange(this.GetVisualAncestors());
        foreach (var source in visibilitySources_)
        {
            source.PropertyChanged += OnVisibilitySourcePropertyChanged;
        }
        wasEffectivelyVisible_ = IsEffectivelyVisible;
    }

    private void ClearVisibilitySubscriptions()
    {
        foreach (var source in visibilitySources_)
        {
            source.PropertyChanged -= OnVisibilitySourcePropertyChanged;
        }
        visibilitySources_.Clear();
        wasEffectivelyVisible_ = false;
    }

    private void OnVisibilitySourcePropertyChanged(
        object? sender,
        AvaloniaPropertyChangedEventArgs change)
    {
        if (!isAttached_ || change.Property != IsVisibleProperty)
        {
            return;
        }

        var isEffectivelyVisible = IsEffectivelyVisible;
        if (isEffectivelyVisible == wasEffectivelyVisible_)
        {
            return;
        }
        wasEffectivelyVisible_ = isEffectivelyVisible;
        if (!isEffectivelyVisible)
        {
            InvalidatePendingPresentation();
            return;
        }

        _ = TryInvalidateExposedSession();
        InvalidatePresentation(resetPresentationEpoch: false);
    }

    private bool TryInvalidateExposedSession() =>
        isAttached_ && IsEffectivelyVisible && Session is { } session &&
        TryInvalidateOpenSession(session, ViewportInvalidationReason.Exposed);

    private static bool TryInvalidateOpenSession(
        ViewportSession session,
        ViewportInvalidationReason reason)
    {
        if (session.Current.IsClosed)
        {
            return false;
        }
        try
        {
            session.Invalidate(reason);
            return true;
        }
        catch (ObjectDisposedException) when (session.Current.IsClosed)
        {
            // Close may race a completion continuation after the UI owner has already stopped
            // admitting work. A closed session is a normal terminal boundary, not degradation.
            return false;
        }
    }

    private void OnPresentationLifetimeResumed(object? sender, EventArgs e)
    {
        if (!ReferenceEquals(sender, subscribedLifetime_))
        {
            return;
        }
        if (Dispatcher.UIThread.CheckAccess())
        {
            ResumePresentationOnUiThread(sender);
            return;
        }
        Dispatcher.UIThread.Post(() => ResumePresentationOnUiThread(sender));
    }

    private void ResumePresentationOnUiThread(object? sender)
    {
        if (!ReferenceEquals(sender, subscribedLifetime_))
        {
            return;
        }
        _ = TryInvalidateExposedSession();
        InvalidatePresentation(resetPresentationEpoch: false);
    }

    private void SetAcquireFailure(ViewportFrameFailure failure)
    {
        var state = failure.Kind switch
        {
            ViewportFrameFailureKind.NativeUnavailable =>
                ViewportPresentationState.NativeUnavailable,
            ViewportFrameFailureKind.DeviceMismatch =>
                ViewportPresentationState.DeviceMismatch,
            ViewportFrameFailureKind.UnsupportedInterop =>
                ViewportPresentationState.Unsupported,
            ViewportFrameFailureKind.UnsupportedFeature =>
                ViewportPresentationState.Unsupported,
            _ => ViewportPresentationState.RenderFailed,
        };
        SetDegraded(state, failure.Message);
    }

    private void SetStatus(ViewportPresentationState state, string message)
    {
        State = state;
        StatusMessage = message;
        IsDegraded = false;
        StateDiagnostics.ObserveStatus(
            state,
            CurrentSessionId,
            generation_,
            RevisionToken);
    }

    private void SetDegraded(ViewportPresentationState state, string message)
    {
        State = state;
        StatusMessage = message;
        IsDegraded = true;
        StateDiagnostics.ObserveDegraded(
            state,
            CurrentSessionId,
            generation_,
            RevisionToken);
    }

    private ViewportPresentationStateDiagnosticTracker StateDiagnostics =>
        stateDiagnostics_ ??= new ViewportPresentationStateDiagnosticTracker(
            StudioAvaloniaDiagnosticHubResolver.RequireCurrent(),
            endpointId_);

    private ViewportSessionId CurrentSessionId =>
        Session?.Current.SessionId ?? default;

    private static ViewportDeviceCompatibility CreateCompatibility(ICompositionGpuInterop interop)
    {
        var luid = interop.DeviceLuid;
        var uuid = interop.DeviceUuid;
        return new ViewportDeviceCompatibility(
            luid is { Length: 8 }
                ? BinaryPrimitives.ReadUInt32LittleEndian(luid.AsSpan(0, 4))
                : 0,
            luid is { Length: 8 }
                ? BinaryPrimitives.ReadInt32LittleEndian(luid.AsSpan(4, 4))
                : 0,
            luid is { Length: 8 },
            uuid is { Length: 16 }
                ? BinaryPrimitives.ReadUInt64LittleEndian(uuid.AsSpan(0, 8))
                : 0,
            uuid is { Length: 16 }
                ? BinaryPrimitives.ReadUInt64LittleEndian(uuid.AsSpan(8, 8))
                : 0,
            uuid is { Length: 16 });
    }

    private static PlatformGraphicsExternalImageProperties CreateImageProperties(
        ViewportFrameLease lease) => new()
        {
            Width = checked((int)lease.AllocationExtent.Width),
            Height = checked((int)lease.AllocationExtent.Height),
            Format = lease.Format switch
            {
                ViewportFrameFormat.Rgba8Unorm =>
                    PlatformGraphicsExternalImageFormat.R8G8B8A8UNorm,
                ViewportFrameFormat.Bgra8Unorm =>
                    PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm,
                _ => throw new ArgumentOutOfRangeException(nameof(lease)),
            },
            MemoryOffset = 0,
            MemorySize = lease.MemorySizeBytes,
            TopLeftOrigin = true,
        };

    private static IEnumerable<StreamPresentationState> DistinctStreams(
        StreamPresentationState? first,
        StreamPresentationState? second)
    {
        if (first is not null)
        {
            yield return first;
        }
        if (second is not null && !ReferenceEquals(first, second))
        {
            yield return second;
        }
    }

    private static async Task DrainDetachedPresentationAsync(
        IReadOnlyList<CompositionDrawingSurface> surfaces,
        Task removalProcessed,
        IReadOnlyList<Task> retirements,
        IDisposable? admission)
    {
        try
        {
            await Task.WhenAll(retirements.Append(removalProcessed));
        }
        finally
        {
            try
            {
                foreach (var surface in surfaces)
                {
                    if (Dispatcher.UIThread.CheckAccess())
                    {
                        surface.Dispose();
                    }
                    else
                    {
                        await Dispatcher.UIThread.InvokeAsync(
                            surface.Dispose,
                            DispatcherPriority.Send);
                    }
                }
            }
            finally
            {
                admission?.Dispose();
            }
        }
    }

    private sealed class ViewportPresentationUnsupportedFeatureException : Exception
    {
        public ViewportPresentationUnsupportedFeatureException(string message)
            : base(message)
        {
        }
    }
}

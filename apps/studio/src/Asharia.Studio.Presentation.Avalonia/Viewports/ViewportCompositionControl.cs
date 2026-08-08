using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Vector3 = System.Numerics.Vector3;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public sealed class ViewportCompositionControl : Control
{
    private enum CompositionCommitResult
    {
        NotSubmittedToConsumer,
        ConsumerAccessed,
        Presented,
    }

    private sealed class StreamPresentationState
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

    private sealed record ImportedSlot(
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
            ICompositionImportedGpuImage image,
            ICompositionImportedGpuSemaphore waitSemaphore,
            ICompositionImportedGpuSemaphore signalSemaphore,
            CompositionConsumerAccessTracker accessTracker,
            Func<bool> canPresent,
            Func<bool> tryMarkPresented)
        {
            Extent = extent;
            GeometryGeneration = geometryGeneration;
            Image = image;
            WaitSemaphore = waitSemaphore;
            SignalSemaphore = signalSemaphore;
            AccessTracker = accessTracker;
            CanPresent = canPresent;
            TryMarkPresented = tryMarkPresented;
        }

        public ViewportExtent Extent { get; }

        public ulong GeometryGeneration { get; }

        public ICompositionImportedGpuImage Image { get; }

        public ICompositionImportedGpuSemaphore WaitSemaphore { get; }

        public ICompositionImportedGpuSemaphore SignalSemaphore { get; }

        public CompositionConsumerAccessTracker AccessTracker { get; }

        public Func<bool> CanPresent { get; }

        public Func<bool> TryMarkPresented { get; }

        public TaskCompletionSource<bool> Completion { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
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
    private readonly List<Task> retiringStreamTasks_ = new();
    private CompositionSurfaceVisual? compositionVisual_;
    private CompositionDrawingSurface? surface_;
    private ICompositionGpuInterop? interop_;
    private ViewportPresentationLifetime? subscribedLifetime_;
    private TopLevel? topLevel_;
    private StreamPresentationState? activeStream_;
    private StreamPresentationState? desiredStream_;
    private Task detachTask_;
    private ViewportPresentationState state_ = ViewportPresentationState.Detached;
    private string statusMessage_ = "Scene View is detached.";
    private bool isDegraded_;
    private bool isAttached_;
    private bool isFrameQueued_;
    private bool isCompositionCommitQueued_;
    private PendingCompositionCommit? pendingCompositionCommit_;
    private ViewportRenderSize lastPresentedSize_;
    private ViewportExtent lastPresentedPanelExtent_;
    private ulong exactExtentPresentedFrames_;
    private ulong rejectedNonExactCandidates_;
    private ulong queuedFrameTicket_;
    private ulong compositionCommitTicket_;
    private ulong generation_;

    public ViewportCompositionControl()
        : this(Task.CompletedTask)
    {
    }

    internal ViewportCompositionControl(Task precedingDetach)
    {
        ArgumentNullException.ThrowIfNull(precedingDetach);
        detachTask_ = precedingDetach;
        ClipToBounds = true;
    }

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
        lastPresentedPanelExtent_);

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
        SynchronizeLifetimeSubscription();
        topLevel_ = TopLevel.GetTopLevel(this);
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged += OnScalingChanged;
        }
        SynchronizeGeometryGeneration();

        var generation = ++generation_;
        presentationState_.Reset(generation);
        cadenceTracker_.Reset();
        lastPresentedSize_ = default;
        lastPresentedPanelExtent_ = default;
        exactExtentPresentedFrames_ = 0;
        rejectedNonExactCandidates_ = 0;
        _ = AttachAsync(generation, detachTask_);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        isFrameQueued_ = false;
        queuedFrameTicket_++;
        pendingCompositionCommit_?.Completion.TrySetResult(false);
        pendingCompositionCommit_ = null;
        isCompositionCommitQueued_ = false;
        compositionCommitTicket_++;
        geometryState_.Invalidate();
        presentationState_.Reset(++generation_);
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
        var surface = surface_;
        surface_ = null;
        compositionVisual_ = null;

        var streams = DistinctStreams(activeStream_, desiredStream_).ToArray();
        activeStream_ = null;
        desiredStream_ = null;
        var retirements = retiringStreamTasks_.ToList();
        retirements.AddRange(streams.Select(BeginRetireStream));
        SetStatus(ViewportPresentationState.Draining, "Scene View presentation is draining.");
        var admission = Lifetime?.BeginCleanup();
        detachTask_ = DrainDetachedPresentationAsync(
            surface,
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
            SynchronizeGeometryGeneration();
            UpdateVisualPlacement();
            InvalidatePresentation(resetPresentationEpoch: false);
        }
        else if (change.Property == SessionProperty)
        {
            InvalidatePresentation(resetPresentationEpoch: true);
        }
        else if (change.Property == RevisionTokenProperty)
        {
            InvalidatePresentation(resetPresentationEpoch: false);
        }
        else if (change.Property == LifetimeProperty)
        {
            SynchronizeLifetimeSubscription();
            InvalidatePresentation(resetPresentationEpoch: true);
        }
        else if (change.Property == IsRealtimeProperty &&
                 IsRealtime && Session is { } realtimeSession)
        {
            realtimeSession.Invalidate(ViewportInvalidationReason.Realtime);
            InvalidatePresentation(resetPresentationEpoch: false);
        }
        else if (change.Property == IsVisibleProperty)
        {
            if (IsVisible && Session is { } session)
            {
                session.Invalidate(ViewportInvalidationReason.Exposed);
            }
            InvalidatePresentation(resetPresentationEpoch: true);
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
        if (Session is null)
        {
            SetStatus(
                ViewportPresentationState.WaitingForDocument,
                "Create or open a project to display its default scene.");
            return;
        }
        SetStatus(ViewportPresentationState.Ready, "Scene View presentation is ready.");
        QueueFrame();
    }

    private void InvalidatePresentation(bool resetPresentationEpoch)
    {
        if (!isAttached_ || interop_ is null)
        {
            return;
        }
        if (resetPresentationEpoch)
        {
            presentationState_.Reset(++generation_);
            if (desiredStream_ is not null && !ReferenceEquals(desiredStream_, activeStream_))
            {
                _ = BeginRetireStream(desiredStream_);
            }
            desiredStream_ = null;
        }
        if (Session is null)
        {
            SetStatus(
                ViewportPresentationState.WaitingForDocument,
                "Create or open a project to display its default scene.");
            return;
        }
        QueueFrame();
    }

    private void QueueFrame()
    {
        if (!isAttached_ || interop_ is null || isFrameQueued_ ||
            compositionVisual_ is not { } visual)
        {
            return;
        }
        isFrameQueued_ = true;
        var ticket = ++queuedFrameTicket_;
        var generation = generation_;
        var compositor = visual.Compositor;
        // Bounds changes only overwrite ViewportSession state. Consume that latest state once
        // before the next Avalonia composition commit instead of turning every layout event into
        // a native render request. Compositor batch backpressure therefore defines the resize
        // submission cadence without a fixed-rate UI timer.
        compositor.RequestCompositionUpdate(
            () => PublishLatestFrame(ticket, generation, compositor));
    }

    private void PublishLatestFrame(
        ulong queuedTicket,
        ulong queuedGeneration,
        Compositor queuedCompositor)
    {
        // A callback from a detached visual must not clear a newer visual's queue latch.
        if (!isFrameQueued_ || queuedFrameTicket_ != queuedTicket)
        {
            return;
        }
        isFrameQueued_ = false;
        if (queuedGeneration != generation_ ||
            compositionVisual_ is not { } visual ||
            !ReferenceEquals(visual.Compositor, queuedCompositor))
        {
            // Generation changes are state replacement, not cancellation of the latest frame.
            // Re-arm the one-shot compositor callback for the current visual.
            QueueFrame();
            return;
        }
        if (!IsCurrent(queuedGeneration) || !IsEffectivelyVisible || interop_ is null ||
            surface_ is null ||
            Session is not { } session || Lifetime is not { } lifetime ||
            !lifetime.IsAcceptingFrames ||
            !TryGetRenderSize(out var renderSize) ||
            !session.TryPublishLatest(renderSize, out var request))
        {
            return;
        }

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
        var submitted = stream.Stream.SubmitLatest(request);
        if (!submitted.Succeeded)
        {
            session.RetryPublishedFrame(request);
            HandleSubmissionFailure(submitted.Failure!);
            return;
        }
        EnsurePumpRunning(generation_, lifetime);
        if (IsRealtime)
        {
            // RequestCompositionUpdate called from inside this callback is queued for the next
            // commit. That overlaps native production of frame N+1 with Avalonia consuming frame
            // N, so a 60 Hz compositor can sustain one scene frame per tick after warm-up.
            session.Invalidate(ViewportInvalidationReason.Realtime);
            QueueFrame();
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
                PromoteStream(stream);
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
        ViewportRenderStream stream)
    {
        while (true)
        {
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
            await Task.Delay(1).ConfigureAwait(false);
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
                    () => TryMarkPresented(lease, generation));
            }
            catch
            {
                if (accessTracker.State == CompositionConsumerAccessState.SubmissionStarted)
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

    private async Task<ImportedSlot> GetOrImportSlotAsync(
        StreamPresentationState stream,
        ViewportFrameLease lease)
    {
        if (stream.ImportedSlots.TryGetValue(lease.SlotIdentity, out var existing))
        {
            if (existing.Handles != lease.NativeHandles)
            {
                throw new InvalidOperationException(
                    "Native viewport changed handles for a persistent V5 slot.");
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
            waitSemaphore = interop.ImportSemaphore(
                new PlatformHandle(lease.NativeHandles.WaitSemaphore, SemaphoreHandleType));
            signalSemaphore = interop.ImportSemaphore(
                new PlatformHandle(lease.NativeHandles.SignalSemaphore, SemaphoreHandleType));
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

    private void PromoteStream(StreamPresentationState stream)
    {
        if (!ReferenceEquals(desiredStream_, stream) || ReferenceEquals(activeStream_, stream))
        {
            return;
        }
        var previous = activeStream_;
        activeStream_ = stream;
        if (previous is not null)
        {
            _ = BeginRetireStream(previous);
        }
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
        ICompositionImportedGpuImage image,
        ICompositionImportedGpuSemaphore waitSemaphore,
        ICompositionImportedGpuSemaphore signalSemaphore,
        CompositionConsumerAccessTracker accessTracker,
        Func<bool> canPresent,
        Func<bool> tryMarkPresented)
    {
        var visual = compositionVisual_;
        var surface = surface_;
        if (visual is null || surface is null || !canPresent())
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }

        var commit = new PendingCompositionCommit(
            extent,
            geometryGeneration,
            image,
            waitSemaphore,
            signalSemaphore,
            accessTracker,
            canPresent,
            tryMarkPresented);
        QueueCompositionCommit(commit, visual, surface);

        if (!await commit.Completion.Task)
        {
            return CompositionCommitResult.NotSubmittedToConsumer;
        }
        var presented = Dispatcher.UIThread.CheckAccess()
            ? commit.TryMarkPresented()
            : await Dispatcher.UIThread.InvokeAsync(
                commit.TryMarkPresented,
                DispatcherPriority.Render);
        return presented
            ? CompositionCommitResult.Presented
            : CompositionCommitResult.ConsumerAccessed;
    }

    private void QueueCompositionCommit(
        PendingCompositionCommit commit,
        CompositionSurfaceVisual visual,
        CompositionDrawingSurface surface)
    {
        // Native production is latest-wins, and the composition boundary must be too. Avalonia's
        // swapchain sample performs one Present per compositor callback; submitting several
        // surface snapshots in the same callback cycle can strand release semaphores.
        pendingCompositionCommit_?.Completion.TrySetResult(false);
        pendingCompositionCommit_ = commit;
        if (isCompositionCommitQueued_)
        {
            return;
        }

        isCompositionCommitQueued_ = true;
        var ticket = ++compositionCommitTicket_;
        visual.Compositor.RequestCompositionUpdate(
            () => PublishCompositionCommit(ticket, visual, surface));
    }

    private void PublishCompositionCommit(
        ulong ticket,
        CompositionSurfaceVisual visual,
        CompositionDrawingSurface surface)
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
            !ReferenceEquals(surface_, surface))
        {
            commit.Completion.TrySetResult(false);
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
            commit.Completion.TrySetResult(false);
            return;
        }

        Task update;
        try
        {
            update = surface.UpdateWithSemaphoresAsync(
                commit.Image,
                commit.WaitSemaphore,
                commit.SignalSemaphore);
            commit.AccessTracker.MarkSubmissionStarted();
            // The bitmap and its destination rectangle are one compositor transaction. Resizing
            // the visual before this exact-size surface update would stretch the previous bitmap
            // for one frame during a panel resize.
            geometryState_.MarkSurfaceUpdate(commit.Extent, commit.GeometryGeneration);
            UpdateVisualPlacement();
        }
        catch (Exception exception)
        {
            commit.Completion.TrySetException(exception);
            return;
        }
        _ = CompleteSurfaceUpdateAsync(update, commit.Completion, commit.AccessTracker);
    }

    private static async Task CompleteSurfaceUpdateAsync(
        Task update,
        TaskCompletionSource<bool> completion,
        CompositionConsumerAccessTracker accessTracker)
    {
        try
        {
            await update.ConfigureAwait(false);
            accessTracker.MarkConsumerAccessed();
            completion.TrySetResult(true);
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
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
    {
        if (!IsCurrent(generation) || !CanPresentAtCurrentGeometry(lease) ||
            Session is not { } session)
        {
            return false;
        }
        return presentationState_.CanPresent(
            generation,
            PresentationFrame(lease),
            session.Current);
    }

    private bool TryMarkPresented(ViewportFrameLease lease, ulong generation)
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
            exactExtentPresentedFrames_++;
            cadenceTracker_.Record(Stopwatch.GetTimestamp());
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
        if (scaling <= 0 || Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return false;
        }

        var width = Math.Clamp(Math.Ceiling(Bounds.Width * scaling), 1, uint.MaxValue);
        var height = Math.Clamp(Math.Ceiling(Bounds.Height * scaling), 1, uint.MaxValue);
        var logicalExtent = new ViewportExtent(
            checked((uint)width),
            checked((uint)height));
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
        visual.Opacity = hasExactSurface ? 1 : 0;
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

    private void OnScalingChanged(object? sender, EventArgs e)
    {
        if (ReferenceEquals(sender, topLevel_))
        {
            SynchronizeGeometryGeneration();
            UpdateVisualPlacement();
            InvalidatePresentation(resetPresentationEpoch: false);
        }
    }

    private void SynchronizeGeometryGeneration()
    {
        if (!TryGetRenderSize(out var currentSize))
        {
            return;
        }
        geometryState_.Synchronize(currentSize.LogicalExtent);
    }

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

    private void OnPresentationLifetimeResumed(object? sender, EventArgs e)
    {
        if (!ReferenceEquals(sender, subscribedLifetime_))
        {
            return;
        }
        if (Dispatcher.UIThread.CheckAccess())
        {
            InvalidatePresentation(resetPresentationEpoch: false);
            return;
        }
        Dispatcher.UIThread.Post(() =>
        {
            if (ReferenceEquals(sender, subscribedLifetime_))
            {
                InvalidatePresentation(resetPresentationEpoch: false);
            }
        });
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
            _ => ViewportPresentationState.RenderFailed,
        };
        SetDegraded(state, failure.Message);
    }

    private void SetStatus(ViewportPresentationState state, string message)
    {
        State = state;
        StatusMessage = message;
        IsDegraded = false;
    }

    private void SetDegraded(ViewportPresentationState state, string message)
    {
        State = state;
        StatusMessage = message;
        IsDegraded = true;
    }

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
        CompositionDrawingSurface? surface,
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
                if (surface is not null)
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
}

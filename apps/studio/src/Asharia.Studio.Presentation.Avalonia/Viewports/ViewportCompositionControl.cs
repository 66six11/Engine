using System;
using System.Buffers.Binary;
using System.Collections.Generic;
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
    private const string ImageHandleType =
        KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle;
    private const string SemaphoreHandleType =
        KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle;

    public static readonly StyledProperty<ViewportSession?> SessionProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ViewportSession?>(nameof(Session));

    public static readonly StyledProperty<ulong> RevisionTokenProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ulong>(nameof(RevisionToken));

    public static readonly StyledProperty<ViewportPresentationLifetime?> LifetimeProperty =
        AvaloniaProperty.Register<ViewportCompositionControl, ViewportPresentationLifetime?>(
            nameof(Lifetime));

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
    private CompositionSurfaceVisual? compositionVisual_;
    private CompositionDrawingSurface? surface_;
    private ICompositionGpuInterop? interop_;
    private ViewportPresentationLifetime? subscribedLifetime_;
    private TopLevel? topLevel_;
    private Task frameTask_ = Task.CompletedTask;
    private Task detachTask_ = Task.CompletedTask;
    private IDisposable? frameAdmission_;
    private ViewportPresentationState state_ = ViewportPresentationState.Detached;
    private string statusMessage_ = "Scene View is detached.";
    private bool isDegraded_;
    private bool isAttached_;
    private bool isFrameQueued_;
    private ulong generation_;

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

        var generation = ++generation_;
        _ = AttachAsync(generation, detachTask_);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        isFrameQueued_ = false;
        generation_++;
        SynchronizeLifetimeSubscription();
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged -= OnScalingChanged;
            topLevel_ = null;
        }

        interop_ = null;
        ElementComposition.SetElementChildVisual(this, null);
        var surface = surface_;
        surface_ = null;
        compositionVisual_ = null;
        SetStatus(ViewportPresentationState.Draining, "Scene View presentation is draining.");
        var admission = Lifetime?.BeginCleanup();
        detachTask_ = DisposeSurfaceAfterFrameAsync(surface, frameTask_, admission);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            UpdateVisualPlacement();
            InvalidatePresentation();
        }
        else if (change.Property == SessionProperty ||
                 change.Property == RevisionTokenProperty)
        {
            InvalidatePresentation();
        }
        else if (change.Property == LifetimeProperty)
        {
            SynchronizeLifetimeSubscription();
            InvalidatePresentation();
        }
        else if (change.Property == IsVisibleProperty)
        {
            if (IsVisible && Session is { } session)
            {
                session.Invalidate(ViewportInvalidationReason.Exposed);
            }
            InvalidatePresentation();
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

    private void InvalidatePresentation()
    {
        if (!isAttached_)
        {
            return;
        }

        generation_++;
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
        if (!isAttached_ || interop_ is null || isFrameQueued_ || !frameTask_.IsCompleted)
        {
            return;
        }

        isFrameQueued_ = true;
        Dispatcher.UIThread.Post(
            BeginQueuedFrame,
            DispatcherPriority.Render);
    }

    private void BeginQueuedFrame()
    {
        isFrameQueued_ = false;
        if (!TryCaptureFrame(
                out var session,
                out var request,
                out var generation,
                out var lifetime))
        {
            return;
        }

        frameTask_ = PresentFrameGuardedAsync(session, request, generation, lifetime);
    }

    private bool TryCaptureFrame(
        out ViewportSession session,
        out ViewportRenderRequest request,
        out ulong generation,
        out ViewportPresentationLifetime lifetime)
    {
        session = Session!;
        request = null!;
        generation = generation_;
        lifetime = Lifetime!;
        if (!isAttached_ || !IsEffectivelyVisible || interop_ is null || surface_ is null ||
            session is null || lifetime is null ||
            !TryGetExtent(out var extent) ||
            !lifetime.TryBeginFrame(out var admission))
        {
            return false;
        }

        if (!session.TryBeginRender(extent, out request))
        {
            admission.Dispose();
            return false;
        }

        frameAdmission_ = admission;
        return true;
    }

    private async Task PresentFrameGuardedAsync(
        ViewportSession session,
        ViewportRenderRequest request,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        var succeeded = false;
        try
        {
            succeeded = await PresentFrameAsync(
                session,
                request,
                generation,
                lifetime);
        }
        catch (Exception exception)
        {
            if (IsCurrent(session, request, generation))
            {
                SetDegraded(
                    ViewportPresentationState.RenderFailed,
                    $"Scene View presentation failed: {exception.Message}");
            }
        }
        finally
        {
            var completionIsCurrent =
                session.CompleteRender(request.Sequence, request.TargetRevision, succeeded);
            frameAdmission_?.Dispose();
            frameAdmission_ = null;
            if (completionIsCurrent ||
                isAttached_ && Session is { } currentSession &&
                (!ReferenceEquals(currentSession, session) ||
                 generation_ != generation ||
                 currentSession.Current.TargetRevision != request.TargetRevision))
            {
                Dispatcher.UIThread.Post(QueueFrame, DispatcherPriority.Render);
            }
        }
    }

    private async Task<bool> PresentFrameAsync(
        ViewportSession session,
        ViewportRenderRequest request,
        ulong generation,
        ViewportPresentationLifetime lifetime)
    {
        var interop = interop_;
        if (interop is null)
        {
            return false;
        }

        var compatibility = CreateCompatibility(interop);
        var acquired = await Task.Run(() => bridge_.CreatePresentSlot(request, compatibility));
        if (!acquired.Succeeded)
        {
            if (IsCurrent(session, request, generation))
            {
                SetAcquireFailure(acquired.Failure!);
            }
            return false;
        }

        var lease = acquired.Lease!;
        var releaseLease = true;
        try
        {
            if (!IsCurrent(session, request, generation))
            {
                return false;
            }

            var handles = lease.NativeHandles;
            ICompositionImportedGpuImage? image = null;
            ICompositionImportedGpuSemaphore? waitSemaphore = null;
            ICompositionImportedGpuSemaphore? signalSemaphore = null;
            var committed = false;
            try
            {
                image = interop.ImportImage(
                    new PlatformHandle(handles.Image, ImageHandleType),
                    CreateImageProperties(lease));
                waitSemaphore = interop.ImportSemaphore(
                    new PlatformHandle(handles.WaitSemaphore, SemaphoreHandleType));
                signalSemaphore = interop.ImportSemaphore(
                    new PlatformHandle(handles.SignalSemaphore, SemaphoreHandleType));
                committed = await CommitAsync(
                    image,
                    waitSemaphore,
                    signalSemaphore,
                    () => IsCurrent(session, request, generation));
            }
            finally
            {
                try
                {
                    await DisposeImportedResourcesAsync(
                        image,
                        waitSemaphore,
                        signalSemaphore);
                }
                catch
                {
                    releaseLease = false;
                    lifetime.QuarantineFrame(
                        lease,
                        image,
                        waitSemaphore,
                        signalSemaphore);
                    throw;
                }
            }

            if (committed)
            {
                lease.Complete();
                SetStatus(
                    ViewportPresentationState.Ready,
                    $"Presented scene revision {request.TargetRevision}.");
            }
            return committed;
        }
        finally
        {
            if (releaseLease)
            {
                lease.Dispose();
            }
        }
    }

    private static async Task DisposeImportedResourcesAsync(
        ICompositionImportedGpuImage? image,
        ICompositionImportedGpuSemaphore? waitSemaphore,
        ICompositionImportedGpuSemaphore? signalSemaphore)
    {
        var failures = new List<Exception>(capacity: 3);
        await DisposeAsync(image, failures);
        await DisposeAsync(waitSemaphore, failures);
        await DisposeAsync(signalSemaphore, failures);
        if (failures.Count == 1)
        {
            ExceptionDispatchInfo.Capture(failures[0]).Throw();
        }
        if (failures.Count > 1)
        {
            throw new AggregateException(
                "Viewport imported resources did not release cleanly.",
                failures);
        }

        static async Task DisposeAsync(
            ICompositionGpuImportedObject? resource,
            List<Exception> failures)
        {
            if (resource is null)
            {
                return;
            }

            try
            {
                await resource.DisposeAsync();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    private Task<bool> CommitAsync(
        ICompositionImportedGpuImage image,
        ICompositionImportedGpuSemaphore waitSemaphore,
        ICompositionImportedGpuSemaphore signalSemaphore,
        Func<bool> isCurrent)
    {
        var visual = compositionVisual_;
        var surface = surface_;
        if (visual is null || surface is null || !isCurrent())
        {
            return Task.FromResult(false);
        }

        var completion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        visual.Compositor.RequestCompositionUpdate(() =>
        {
            if (!ReferenceEquals(surface_, surface) || !isCurrent())
            {
                completion.TrySetResult(false);
                return;
            }

            Task update;
            try
            {
                update = surface.UpdateWithSemaphoresAsync(
                    image,
                    waitSemaphore,
                    signalSemaphore);
                UpdateVisualPlacement();
            }
            catch (Exception exception)
            {
                completion.TrySetException(exception);
                return;
            }

            _ = CompleteCompositionUpdateAsync(update, isCurrent, completion);
        });
        return completion.Task;
    }

    private static async Task CompleteCompositionUpdateAsync(
        Task update,
        Func<bool> isCurrent,
        TaskCompletionSource<bool> completion)
    {
        try
        {
            await update;
            completion.TrySetResult(isCurrent());
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }

    private bool IsCurrent(ulong generation) =>
        isAttached_ && generation_ == generation;

    private bool IsCurrent(
        ViewportSession session,
        ViewportRenderRequest request,
        ulong generation) =>
        IsCurrent(generation) &&
        ReferenceEquals(Session, session) &&
        session.Current.TargetRevision == request.TargetRevision;

    private bool TryGetExtent(out ViewportExtent extent)
    {
        extent = default;
        var scaling = topLevel_?.RenderScaling ?? 0;
        if (scaling <= 0 || Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return false;
        }

        var width = Math.Clamp(Math.Round(Bounds.Width * scaling), 1, uint.MaxValue);
        var height = Math.Clamp(Math.Round(Bounds.Height * scaling), 1, uint.MaxValue);
        extent = new ViewportExtent(checked((uint)width), checked((uint)height));
        return true;
    }

    private void UpdateVisualPlacement()
    {
        if (compositionVisual_ is not { } visual)
        {
            return;
        }

        visual.Offset = Vector3.Zero;
        visual.Size = new Vector((float)Math.Max(0, Bounds.Width), (float)Math.Max(0, Bounds.Height));
    }

    private void OnScalingChanged(object? sender, EventArgs e)
    {
        if (ReferenceEquals(sender, topLevel_))
        {
            InvalidatePresentation();
        }
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
            InvalidatePresentation();
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (ReferenceEquals(sender, subscribedLifetime_))
            {
                InvalidatePresentation();
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

    private static ViewportDeviceCompatibility CreateCompatibility(
        ICompositionGpuInterop interop)
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
        Width = checked((int)lease.Extent.Width),
        Height = checked((int)lease.Extent.Height),
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

    private static async Task DisposeSurfaceAfterFrameAsync(
        CompositionDrawingSurface? surface,
        Task frame,
        IDisposable? admission)
    {
        try
        {
            await frame;
            surface?.Dispose();
        }
        finally
        {
            admission?.Dispose();
        }
    }
}

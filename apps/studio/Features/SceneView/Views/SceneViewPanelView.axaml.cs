using System;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Rendering.Composition;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Models.Viewports;
using Editor.Features.SceneView.Interop;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.SceneView.Views;

public partial class SceneViewPanelView : UserControl
{
    private readonly SceneViewCompositionCapabilityReader compositionReader_ = new();
    private readonly ViewportNativeBridge nativeBridge_ = new();
    private readonly SceneViewPresentationSession presentationSession_;
    private SceneViewPanelViewModel? frameSourceViewModel_;
    private ICompositionGpuInterop? compositionInterop_;
    private ViewportCompositionCapabilitiesSnapshot? compositionCapabilities_;
    private TopLevel? presentationTopLevel_;
    private Task detachTask_ = Task.CompletedTask;
    private bool isRetryQueued_;
    private bool isAttached_;
    private bool isSessionAttached_;
    private PresentationSetupState presentationSetup_;
    private ulong retryQueueSequence_;
    private ulong probeSequence_;

    public SceneViewPanelView()
    {
        InitializeComponent();
        presentationSession_ =
            new SceneViewPresentationSession(nativeBridge_, CompositionHost);
        CompositionHost.SizeChanged += OnCompositionHostSizeChanged;
        DataContextChanged += OnDataContextChanged;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached_ = true;
        presentationTopLevel_ = TopLevel.GetTopLevel(this);
        if (presentationTopLevel_ is not null)
        {
            presentationTopLevel_.ScalingChanged += OnTopLevelScalingChanged;
        }

        SetFrameSourceViewModel(DataContext as SceneViewPanelViewModel);
        BeginPresentationAttach();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        isRetryQueued_ = false;
        retryQueueSequence_++;
        if (presentationTopLevel_ is not null)
        {
            presentationTopLevel_.ScalingChanged -= OnTopLevelScalingChanged;
            presentationTopLevel_ = null;
        }

        probeSequence_++;
        presentationSetup_ = PresentationSetupState.Detached;
        compositionInterop_ = null;
        compositionCapabilities_ = null;
        SetFrameSourceViewModel(null);
        isSessionAttached_ = false;
        var presentationDrain = presentationSession_.DetachAsync();
        detachTask_ =
            CompositionHost.ReleaseCompositionResourcesAsync(
                presentationDrain);
        _ = ViewportNativePresentDrain.TrackAsync(detachTask_);
        base.OnDetachedFromVisualTree(e);
    }

    private void BeginPresentationAttach()
    {
        var probeSequence = ++probeSequence_;
        _ = AttachAndProbeAsync(probeSequence, detachTask_);
    }

    private async Task AttachAndProbeAsync(
        ulong probeSequence,
        Task precedingDetach)
    {
        await precedingDetach;
        if (!IsCurrentAttachment(probeSequence))
        {
            return;
        }

        presentationSession_.Attach();
        isSessionAttached_ = true;
        presentationSetup_ = PresentationSetupState.ProbingCapabilities;
        await ProbeCompositionCapabilitiesAsync(probeSequence);
    }

    private void BeginCapabilityProbe()
    {
        if (!isSessionAttached_)
        {
            BeginPresentationAttach();
            return;
        }

        compositionCapabilities_ = null;
        presentationSetup_ = PresentationSetupState.ProbingCapabilities;
        var probeSequence = ++probeSequence_;
        _ = ProbeCompositionCapabilitiesAsync(probeSequence);
    }

    private async Task ProbeCompositionCapabilitiesAsync(ulong probeSequence)
    {
        if (DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        try
        {
            var capabilities =
                await compositionReader_.ReadAsync(this, viewModel.ViewportId);
            if (!IsCurrentProbe(viewModel, probeSequence))
            {
                return;
            }

            viewModel.UpdateCompositionCapabilities(capabilities);
            compositionCapabilities_ = capabilities;
            if (capabilities.Status != ViewportCompositionStatus.Supported)
            {
                presentationSetup_ = PresentationSetupState.Unavailable;
                return;
            }

            await ConfigurePresentationAsync(
                viewModel,
                capabilities,
                probeSequence);
        }
        catch (Exception ex)
        {
            if (!IsCurrentProbe(viewModel, probeSequence))
            {
                return;
            }

            presentationSetup_ = PresentationSetupState.Unavailable;
            viewModel.UpdateCompositionCapabilities(
                CreateLocalCompositionSnapshot(
                    viewModel,
                    ViewportCompositionStatus.GpuInteropUnavailable,
                    CreateExceptionMessage(
                        "Scene View presentation setup failed",
                        ex)));
        }
    }

    private void BeginPresentationConfiguration()
    {
        if (presentationSetup_ != PresentationSetupState.WaitingForFrameExtent ||
            compositionCapabilities_ is not
            {
                Status: ViewportCompositionStatus.Supported,
            } capabilities ||
            DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        presentationSetup_ = PresentationSetupState.Configuring;
        var probeSequence = ++probeSequence_;
        _ = ConfigurePresentationAsync(
            viewModel,
            capabilities,
            probeSequence);
    }

    private async Task ConfigurePresentationAsync(
        SceneViewPanelViewModel viewModel,
        ViewportCompositionCapabilitiesSnapshot capabilities,
        ulong probeSequence)
    {
        try
        {
            var observation = TryCaptureFrameObservation(viewModel);
            if (observation is null)
            {
                if (IsCurrentProbe(viewModel, probeSequence))
                {
                    presentationSetup_ =
                        PresentationSetupState.WaitingForFrameExtent;
                }

                return;
            }

            presentationSetup_ = PresentationSetupState.Configuring;
            var compatibility =
                await Task.Run(
                    () =>
                        nativeBridge_.QueryCompositionCompatibility(
                            capabilities,
                            observation.PixelExtent));
            if (!IsCurrentProbe(viewModel, probeSequence))
            {
                return;
            }

            viewModel.UpdateNativePresent(compatibility);
            if (compatibility.Status != ViewportNativePresentStatus.Success)
            {
                presentationSetup_ = PresentationSetupState.Unavailable;
                return;
            }

            var interop = await TryGetCompositionGpuInteropAsync(CompositionHost);
            if (!IsCurrentProbe(viewModel, probeSequence))
            {
                return;
            }

            if (interop is null)
            {
                presentationSetup_ = PresentationSetupState.Unavailable;
                viewModel.UpdateNativePresent(
                    CreateLocalPresentSnapshot(
                        viewModel,
                        observation,
                        ViewportNativePresentStatus.ImportFailed,
                        "Avalonia composition GPU interop is unavailable for the Scene View surface."));
                return;
            }

            if (interop.IsLost)
            {
                presentationSetup_ = PresentationSetupState.Unavailable;
                viewModel.UpdateNativePresent(
                    CreateLocalPresentSnapshot(
                        viewModel,
                        observation,
                        ViewportNativePresentStatus.DeviceLost,
                        "Avalonia composition GPU interop device is lost."));
                return;
            }

            compositionInterop_ = interop;
            presentationSetup_ = PresentationSetupState.Configured;
            presentationSession_.Configure(
                interop,
                capabilities,
                snapshot =>
                {
                    if (!IsCurrentProbe(viewModel, probeSequence))
                    {
                        return;
                    }

                    viewModel.UpdateNativePresent(snapshot);
                    if (snapshot.Status == ViewportNativePresentStatus.DeviceLost)
                    {
                        presentationSetup_ = PresentationSetupState.Unavailable;
                        compositionInterop_ = null;
                        presentationSession_.ResetConfiguration();
                    }
                },
                QueueNativeFrameRetry);
            RequestNativeFrame();
        }
        catch (Exception ex)
        {
            if (!IsCurrentProbe(viewModel, probeSequence))
            {
                return;
            }

            presentationSetup_ = PresentationSetupState.Unavailable;
            viewModel.UpdateCompositionCapabilities(
                CreateLocalCompositionSnapshot(
                    viewModel,
                    ViewportCompositionStatus.GpuInteropUnavailable,
                    CreateExceptionMessage(
                        "Scene View presentation configuration failed",
                        ex)));
        }
    }

    private void QueueNativeFrameRetry()
    {
        if (!isAttached_ ||
            presentationSetup_ != PresentationSetupState.Configured ||
            isRetryQueued_)
        {
            return;
        }

        var compositor =
            ElementComposition.GetElementVisual(CompositionHost)?.Compositor;
        if (compositor is null)
        {
            return;
        }

        isRetryQueued_ = true;
        var queueSequence = ++retryQueueSequence_;
        compositor.RequestCompositionUpdate(
            () => CompleteQueuedFrameRetry(queueSequence));
    }

    private void CompleteQueuedFrameRetry(ulong queueSequence)
    {
        if (queueSequence != retryQueueSequence_)
        {
            return;
        }

        isRetryQueued_ = false;
        RequestNativeFrame();
    }

    private void RequestNativeFrame()
    {
        if (presentationSetup_ != PresentationSetupState.Configured ||
            compositionInterop_ is not { } interop ||
            DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        if (interop.IsLost)
        {
            presentationSetup_ = PresentationSetupState.Unavailable;
            compositionInterop_ = null;
            presentationSession_.ResetConfiguration();
            var lostDeviceObservation = TryCaptureFrameObservation(viewModel);
            if (lostDeviceObservation is not null)
            {
                viewModel.UpdateNativePresent(
                    CreateLocalPresentSnapshot(
                        viewModel,
                        lostDeviceObservation,
                        ViewportNativePresentStatus.DeviceLost,
                        "Avalonia composition GPU interop device is lost."));
            }

            return;
        }

        SceneViewFrameObservation? observation = null;
        try
        {
            observation = TryCaptureFrameObservation(viewModel);
            if (observation is not null)
            {
                presentationSession_.RequestFrame(observation);
            }
        }
        catch (Exception ex)
        {
            if (observation is not null)
            {
                viewModel.UpdateNativePresent(
                    CreateLocalPresentSnapshot(
                        viewModel,
                        observation,
                        ViewportNativePresentStatus.RenderFailed,
                        CreateExceptionMessage(
                            "Scene View frame request failed",
                            ex)));
            }
        }
    }

    private SceneViewFrameObservation? TryCaptureFrameObservation(
        SceneViewPanelViewModel viewModel)
    {
        if (!IsVisible || presentationTopLevel_ is not { } topLevel)
        {
            return null;
        }

        var scene = viewModel.GetSceneRenderState();
        return SceneViewFrameObservation.TryCreate(
            viewModel.ViewportId,
            CompositionHost.Bounds.Size,
            topLevel.RenderScaling,
            scene.HasScene,
            scene.Revision);
    }

    private bool IsCurrentProbe(
        SceneViewPanelViewModel viewModel,
        ulong probeSequence)
    {
        return IsCurrentAttachment(probeSequence) &&
               isSessionAttached_ &&
               ReferenceEquals(DataContext, viewModel);
    }

    private bool IsCurrentAttachment(ulong probeSequence)
    {
        return isAttached_ &&
               probeSequence_ == probeSequence &&
               presentationTopLevel_ is not null &&
               ReferenceEquals(
                   TopLevel.GetTopLevel(this),
                   presentationTopLevel_);
    }

    private static async ValueTask<ICompositionGpuInterop?> TryGetCompositionGpuInteropAsync(
        Visual host)
    {
        var compositor = ElementComposition.GetElementVisual(host)?.Compositor
            ?? Compositor.TryGetDefaultCompositor();
        return compositor is null
            ? null
            : await compositor.TryGetCompositionGpuInterop();
    }

    private static ViewportNativePresentSnapshot CreateLocalPresentSnapshot(
        SceneViewPanelViewModel viewModel,
        SceneViewFrameObservation observation,
        ViewportNativePresentStatus status,
        string message)
    {
        return new ViewportNativePresentSnapshot(
            viewModel.ViewportId,
            observation.PixelExtent,
            actualExtent: null,
            formatName: "Unknown",
            colorSpace: "Unknown",
            frameIndex: 0UL,
            status,
            message,
            DateTimeOffset.UtcNow);
    }

    private static ViewportCompositionCapabilitiesSnapshot CreateLocalCompositionSnapshot(
        SceneViewPanelViewModel viewModel,
        ViewportCompositionStatus status,
        string message)
    {
        return new ViewportCompositionCapabilitiesSnapshot(
            viewModel.ViewportId,
            status,
            deviceLuid: null,
            deviceUuid: null,
            imageHandleTypes: [],
            semaphoreHandleTypes: [],
            synchronizationCapabilities: [],
            message,
            DateTimeOffset.UtcNow);
    }

    private static string CreateExceptionMessage(string prefix, Exception ex)
    {
        return string.IsNullOrWhiteSpace(ex.Message)
            ? $"{prefix}."
            : $"{prefix}: {ex.Message}";
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        SetFrameSourceViewModel(DataContext as SceneViewPanelViewModel);
        if (!isAttached_)
        {
            return;
        }

        presentationSetup_ = PresentationSetupState.ProbingCapabilities;
        compositionInterop_ = null;
        compositionCapabilities_ = null;
        presentationSession_.ResetConfiguration();
        BeginCapabilityProbe();
    }

    private void SetFrameSourceViewModel(SceneViewPanelViewModel? viewModel)
    {
        if (ReferenceEquals(frameSourceViewModel_, viewModel))
        {
            return;
        }

        if (frameSourceViewModel_ is not null)
        {
            frameSourceViewModel_.RenderRequested -= OnSceneViewRenderRequested;
        }

        frameSourceViewModel_ = viewModel;
        if (frameSourceViewModel_ is not null)
        {
            frameSourceViewModel_.RenderRequested += OnSceneViewRenderRequested;
        }
    }

    private void OnSceneViewRenderRequested(object? sender, EventArgs e)
    {
        RequestNativeFrame();
    }

    private void OnCompositionHostSizeChanged(
        object? sender,
        SizeChangedEventArgs e)
    {
        RequestFrameForPresentationChange();
    }

    private void OnTopLevelScalingChanged(object? sender, EventArgs e)
    {
        if (ReferenceEquals(sender, presentationTopLevel_))
        {
            RequestFrameForPresentationChange();
        }
    }

    private void RequestFrameForPresentationChange()
    {
        if (!isAttached_)
        {
            return;
        }

        if (presentationSetup_ == PresentationSetupState.Configured)
        {
            RequestNativeFrame();
        }
        else if (presentationSetup_ == PresentationSetupState.WaitingForFrameExtent)
        {
            BeginPresentationConfiguration();
        }
    }

    private enum PresentationSetupState
    {
        Detached,
        ProbingCapabilities,
        WaitingForFrameExtent,
        Configuring,
        Configured,
        Unavailable,
    }
}

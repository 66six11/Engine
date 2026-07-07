using System;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Interop.Viewports.Api;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Viewports;
using Editor.Features.SceneView.Interop;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.SceneView.Views;

public partial class SceneViewPanelView : UserControl
{
    private readonly SceneViewCompositionCapabilityReader compositionReader_ = new();
    private readonly ViewportNativeBridge nativeBridge_ = new();
    private readonly SceneViewNativeViewportLifecycle viewportLifecycle_ = new();
    private readonly SceneViewCompositionPresenter presenter_;
    private SceneViewPanelViewModel? frameSourceViewModel_;

    public SceneViewPanelView()
    {
        InitializeComponent();
        presenter_ = new SceneViewCompositionPresenter(nativeBridge_);
        DataContextChanged += OnDataContextChanged;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        ProbeCompositionCapabilities();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        SetFrameSourceViewModel(null);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            ProbeCompositionCapabilities();
        }
    }

    private void ProbeCompositionCapabilities()
    {
        _ = ProbeCompositionCapabilitiesAsync();
    }

    private async Task ProbeCompositionCapabilitiesAsync()
    {
        if (DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        try
        {
            var snapshot = await compositionReader_.ReadAsync(this, viewModel.ViewportId);
            viewModel.UpdateCompositionCapabilities(snapshot);
            if (snapshot.Status != ViewportCompositionStatus.Supported)
            {
                return;
            }

            var requestedExtent = TryCreateViewportExtent();
            if (requestedExtent is null)
            {
                return;
            }

            var nativeSnapshot = nativeBridge_.QueryCompositionCompatibility(snapshot, requestedExtent);
            viewModel.UpdateNativePresent(nativeSnapshot);
            if (nativeSnapshot.Status != ViewportNativePresentStatus.Success)
            {
                return;
            }

            await TryStartNativePresentAsync(viewModel, snapshot, requestedExtent);
        }
        catch (Exception ex)
        {
            viewModel.UpdateCompositionCapabilities(
                CreateLocalCompositionSnapshot(
                    viewModel.ViewportId,
                    ViewportCompositionStatus.GpuInteropUnavailable,
                    CreateExceptionMessage("Scene View composition capability probe failed", ex)));
        }
    }

    private async Task TryPresentNativeFrameFromCurrentStateAsync()
    {
        if (DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        try
        {
            await TryPresentNativeFrameFromCurrentStateCoreAsync(viewModel);
        }
        catch (Exception ex)
        {
            viewModel.UpdateNativePresent(
                CreateLocalPresentSnapshot(
                    viewModel.ViewportId,
                    viewModel.NativePresent?.RequestedExtent ?? new ViewportExtent(1, 1, renderScale: 1),
                    ViewportNativePresentStatus.RenderFailed,
                    CreateExceptionMessage("Scene View native frame present failed", ex)));
        }
    }

    private async Task TryPresentNativeFrameFromCurrentStateCoreAsync(SceneViewPanelViewModel viewModel)
    {
        if (viewModel.CompositionCapabilities is not
            {
                Status: ViewportCompositionStatus.Supported,
            } compositionCapabilities)
        {
            return;
        }

        if (viewModel.NativePresent?.Status != ViewportNativePresentStatus.Success)
        {
            return;
        }

        var requestedExtent = TryCreateViewportExtent();
        if (requestedExtent is null)
        {
            return;
        }

        await TryStartNativePresentAsync(viewModel, compositionCapabilities, requestedExtent);
    }

    private async Task TryStartNativePresentAsync(
        SceneViewPanelViewModel viewModel,
        ViewportCompositionCapabilitiesSnapshot compositionCapabilities,
        ViewportExtent requestedExtent)
    {
        if (!viewportLifecycle_.CanBeginPresent)
        {
            return;
        }

        var surface = CompositionHost.Surface;
        if (surface is null)
        {
            return;
        }

        var interop = await TryGetCompositionGpuInteropAsync(CompositionHost);
        if (interop is null)
        {
            viewModel.UpdateNativePresent(
                CreateLocalPresentSnapshot(
                    viewModel.ViewportId,
                    requestedExtent,
                    ViewportNativePresentStatus.ImportFailed,
                    "Avalonia composition GPU interop is unavailable for the Scene View surface."));
            return;
        }

        if (interop.IsLost)
        {
            viewModel.UpdateNativePresent(
                CreateLocalPresentSnapshot(
                    viewModel.ViewportId,
                    requestedExtent,
                    ViewportNativePresentStatus.DeviceLost,
                    "Avalonia composition GPU interop device is lost."));
            return;
        }

        if (!viewportLifecycle_.CanBeginPresent)
        {
            return;
        }

        var packet = nativeBridge_.AcquirePresentPacket(compositionCapabilities, requestedExtent);
        if (packet.Status != ViewportNativeStatus.Success)
        {
            viewModel.UpdateNativePresent(
                nativeBridge_.SnapshotAndReleasePresentPacket(
                    packet,
                    viewModel.ViewportId,
                    requestedExtent));
            return;
        }

        if (!viewportLifecycle_.TryBeginPresent(
                () => PresentAndUpdateAsync(viewModel, interop, surface, requestedExtent, packet)))
        {
            nativeBridge_.ReleasePresentPacket(packet);
        }
    }

    private async Task PresentAndUpdateAsync(
        SceneViewPanelViewModel viewModel,
        ICompositionGpuInterop interop,
        CompositionDrawingSurface surface,
        ViewportExtent requestedExtent,
        ViewportNativePresentPacket packet)
    {
        try
        {
            var snapshot = await presenter_.PresentAsync(
                interop,
                surface,
                viewModel.ViewportId,
                requestedExtent,
                packet);
            viewModel.UpdateNativePresent(snapshot);
        }
        catch (Exception ex)
        {
            viewModel.UpdateNativePresent(
                CreateLocalPresentSnapshot(
                    viewModel.ViewportId,
                    requestedExtent,
                    ViewportNativePresentStatus.ImportFailed,
                    ex.Message));
        }
    }

    private ViewportExtent? TryCreateViewportExtent()
    {
        if (!IsVisible)
        {
            return null;
        }

        var bounds = CompositionHost.Bounds;
        if (bounds.Width <= 0 || bounds.Height <= 0 || !double.IsFinite(bounds.Width) || !double.IsFinite(bounds.Height))
        {
            return null;
        }

        var renderScale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
        if (renderScale <= 0 || !double.IsFinite(renderScale))
        {
            return null;
        }

        var widthPixels = checked((int)Math.Ceiling(bounds.Width * renderScale));
        var heightPixels = checked((int)Math.Ceiling(bounds.Height * renderScale));
        if (widthPixels <= 0 || heightPixels <= 0)
        {
            return null;
        }

        return new ViewportExtent(widthPixels, heightPixels, renderScale);
    }

    private static async ValueTask<ICompositionGpuInterop?> TryGetCompositionGpuInteropAsync(Visual host)
    {
        var compositor = ElementComposition.GetElementVisual(host)?.Compositor
            ?? Compositor.TryGetDefaultCompositor();
        return compositor is null
            ? null
            : await compositor.TryGetCompositionGpuInterop();
    }

    private static ViewportNativePresentSnapshot CreateLocalPresentSnapshot(
        ViewportId viewportId,
        ViewportExtent requestedExtent,
        ViewportNativePresentStatus status,
        string message)
    {
        return new ViewportNativePresentSnapshot(
            viewportId,
            requestedExtent,
            actualExtent: null,
            formatName: "Unknown",
            colorSpace: "Unknown",
            frameIndex: 0UL,
            status,
            message,
            DateTimeOffset.UtcNow);
    }

    private static ViewportCompositionCapabilitiesSnapshot CreateLocalCompositionSnapshot(
        ViewportId viewportId,
        ViewportCompositionStatus status,
        string message)
    {
        return new ViewportCompositionCapabilitiesSnapshot(
            viewportId,
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
    }

    private void SetFrameSourceViewModel(SceneViewPanelViewModel? viewModel)
    {
        if (ReferenceEquals(frameSourceViewModel_, viewModel))
        {
            return;
        }

        if (frameSourceViewModel_ is not null)
        {
            frameSourceViewModel_.FrameRequested -= OnSceneViewFrameRequested;
        }

        frameSourceViewModel_ = viewModel;
        if (frameSourceViewModel_ is not null)
        {
            frameSourceViewModel_.FrameRequested += OnSceneViewFrameRequested;
        }
    }

    private void OnSceneViewFrameRequested(object? sender, EditorPanelFrameContext context)
    {
        _ = TryPresentNativeFrameFromCurrentStateAsync();
        context.RequestRepaint();
    }
}

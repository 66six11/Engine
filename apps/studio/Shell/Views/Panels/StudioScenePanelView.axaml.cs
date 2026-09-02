using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.ViewModels.Panels;

namespace Editor.Shell.Views.Panels;

public partial class StudioScenePanelView : UserControl
{
    private const double PickTolerance = 6.0;
    private readonly SceneViewportClickGesture clickGesture_ = new();
    private readonly SceneViewportCameraGesture cameraGesture_ = new();

    public StudioScenePanelView()
    {
        InitializeComponent();
    }

    private void OnSceneViewportPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var current = e.GetCurrentPoint(SceneViewport);
        var properties = current.Properties;
        if (DataContext is StudioScenePanelViewModel { Session: not null } &&
            cameraGesture_.TryBegin(
                e.Pointer.Id,
                current.Position,
                properties.IsLeftButtonPressed,
                properties.IsMiddleButtonPressed,
                properties.IsRightButtonPressed,
                e.KeyModifiers))
        {
            SceneViewport.Focus(NavigationMethod.Pointer);
            e.Pointer.Capture(SceneViewport);
            e.Handled = true;
            return;
        }

        if (!clickGesture_.TryBegin(
                e.Pointer.Id,
                current.Position,
                properties.IsLeftButtonPressed &&
                !properties.IsRightButtonPressed &&
                !properties.IsMiddleButtonPressed,
                e.KeyModifiers))
        {
            return;
        }

        e.Pointer.Capture(SceneViewport);
        e.Handled = true;
    }

    private void OnSceneViewportPointerMoved(object? sender, PointerEventArgs e)
    {
        if (cameraGesture_.Update(
                e.Pointer.Id,
                e.GetPosition(SceneViewport),
                SceneViewport.Bounds.Size,
                out var navigationDelta))
        {
            e.Handled = true;
            if (navigationDelta is { } delta &&
                DataContext is StudioScenePanelViewModel viewModel)
            {
                _ = viewModel.TryApplyCameraNavigation(delta);
            }
            return;
        }

        clickGesture_.Update(e.Pointer.Id, e.GetPosition(SceneViewport));
    }

    private void OnSceneViewportPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (cameraGesture_.Complete(e.Pointer.Id))
        {
            e.Pointer.Capture(null);
            e.Handled = true;
            return;
        }

        var position = e.GetPosition(SceneViewport);
        var accepted = clickGesture_.Complete(
            e.Pointer.Id,
            position,
            SceneViewport.Bounds.Size,
            e.KeyModifiers);
        e.Pointer.Capture(null);
        if (!accepted)
        {
            return;
        }

        e.Handled = true;
        if (DataContext is not StudioScenePanelViewModel viewModel ||
            !SceneViewport.TryCapturePresentedInteractionContext(out var context))
        {
            return;
        }

        _ = viewModel.TryApplyViewportPick(context, CreatePickRequest(context, position));
    }

    private void OnSceneViewportPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (DataContext is not StudioScenePanelViewModel viewModel ||
            !SceneViewportCameraGesture.TryCreateWheelDelta(
                e.Delta.Y,
                SceneViewport.Bounds.Size,
                e.KeyModifiers,
                out var delta) ||
            !viewModel.TryApplyCameraNavigation(delta))
        {
            return;
        }

        SceneViewport.Focus(NavigationMethod.Pointer);
        e.Handled = true;
    }

    internal static ViewportPickRequest CreatePickRequest(
        ViewportPresentedInteractionContext context,
        Point position) =>
        new(
            context.Extent,
            new ViewportPickPoint(
                checked((float)(position.X * context.RenderScaling)),
                checked((float)(position.Y * context.RenderScaling))),
            checked((float)(PickTolerance * context.RenderScaling)));

    private void OnSceneViewportPointerCaptureLost(
        object? sender,
        PointerCaptureLostEventArgs e)
    {
        clickGesture_.Cancel(e.Pointer.Id);
        cameraGesture_.Cancel(e.Pointer.Id);
    }

    private void OnSceneViewportLostFocus(object? sender, RoutedEventArgs e)
    {
        clickGesture_.Cancel();
        cameraGesture_.Cancel();
    }
}

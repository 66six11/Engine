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
    private const double GizmoPickTolerance = 8.0;
    private readonly SceneViewportClickGesture clickGesture_ = new();
    private readonly SceneViewportCameraGesture cameraGesture_ = new();
    private readonly SceneViewportGizmoGesture gizmoGesture_ = new();
    private IPointer? gizmoPointer_;

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
            if (DataContext is StudioScenePanelViewModel cameraViewModel)
            {
                cameraViewModel.ClearTranslateGizmoHover();
            }
            e.Pointer.Capture(SceneViewport);
            e.Handled = true;
            return;
        }

        if (properties.IsLeftButtonPressed &&
            !properties.IsMiddleButtonPressed &&
            !properties.IsRightButtonPressed &&
            e.KeyModifiers == KeyModifiers.None &&
            DataContext is StudioScenePanelViewModel viewModel &&
            SceneViewport.TryCapturePresentedInteractionContext(out var context) &&
            gizmoGesture_.TryBegin(e.Pointer.Id, context))
        {
            if (viewModel.TryBeginTranslateGizmo(
                    context,
                    CreateGizmoPickRequest(context, current.Position)))
            {
                SceneViewport.Focus(NavigationMethod.Pointer);
                gizmoPointer_ = e.Pointer;
                e.Pointer.Capture(SceneViewport);
                e.Handled = true;
                return;
            }
            _ = gizmoGesture_.Cancel(e.Pointer.Id);
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

        if (gizmoGesture_.TryMapPoint(
                e.Pointer.Id,
                e.GetPosition(SceneViewport),
                out var gizmoPoint))
        {
            e.Handled = true;
            if (DataContext is StudioScenePanelViewModel viewModel)
            {
                _ = viewModel.TryUpdateTranslateGizmo(gizmoPoint);
            }
            return;
        }

        clickGesture_.Update(e.Pointer.Id, e.GetPosition(SceneViewport));

        var current = e.GetCurrentPoint(SceneViewport);
        var properties = current.Properties;
        if (!properties.IsLeftButtonPressed &&
            !properties.IsMiddleButtonPressed &&
            !properties.IsRightButtonPressed &&
            e.KeyModifiers == KeyModifiers.None &&
            DataContext is StudioScenePanelViewModel hoverViewModel &&
            SceneViewport.TryCapturePresentedInteractionContext(out var context))
        {
            _ = hoverViewModel.TryUpdateTranslateGizmoHover(
                context,
                CreateGizmoPickRequest(context, current.Position));
            return;
        }

        if (DataContext is StudioScenePanelViewModel clearHoverViewModel)
        {
            clearHoverViewModel.ClearTranslateGizmoHover();
        }
    }

    private void OnSceneViewportPointerExited(object? sender, PointerEventArgs e)
    {
        if (DataContext is StudioScenePanelViewModel viewModel)
        {
            viewModel.ClearTranslateGizmoHover();
        }
    }

    private void OnSceneViewportPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (cameraGesture_.Complete(e.Pointer.Id))
        {
            e.Pointer.Capture(null);
            e.Handled = true;
            return;
        }

        if (gizmoGesture_.Complete(e.Pointer.Id))
        {
            gizmoPointer_ = null;
            if (DataContext is StudioScenePanelViewModel gizmoViewModel)
            {
                _ = gizmoViewModel.CompleteTranslateGizmoAsync();
            }
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
        viewModel.ClearTranslateGizmoHover();
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

    internal static ViewportPickRequest CreateGizmoPickRequest(
        ViewportPresentedInteractionContext context,
        Point position) =>
        new(
            context.Extent,
            new ViewportPickPoint(
                checked((float)(position.X * context.RenderScaling)),
                checked((float)(position.Y * context.RenderScaling))),
            checked((float)(GizmoPickTolerance * context.RenderScaling)));

    private void OnSceneViewportPointerCaptureLost(
        object? sender,
        PointerCaptureLostEventArgs e)
    {
        clickGesture_.Cancel(e.Pointer.Id);
        cameraGesture_.Cancel(e.Pointer.Id);
        if (gizmoGesture_.Cancel(e.Pointer.Id))
        {
            gizmoPointer_ = null;
            if (DataContext is StudioScenePanelViewModel viewModel)
            {
                viewModel.CancelTranslateGizmo();
            }
        }
    }

    private void OnSceneViewportLostFocus(object? sender, RoutedEventArgs e)
    {
        clickGesture_.Cancel();
        cameraGesture_.Cancel();
        CancelTranslateGizmo();
        if (DataContext is StudioScenePanelViewModel viewModel)
        {
            viewModel.ClearTranslateGizmoHover();
        }
    }

    private void OnSceneViewportKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || !CancelTranslateGizmo())
        {
            return;
        }

        e.Handled = true;
    }

    private bool CancelTranslateGizmo()
    {
        if (!gizmoGesture_.Cancel())
        {
            return false;
        }

        var pointer = gizmoPointer_;
        gizmoPointer_ = null;
        if (DataContext is StudioScenePanelViewModel viewModel)
        {
            viewModel.CancelTranslateGizmo();
        }
        pointer?.Capture(null);
        return true;
    }
}

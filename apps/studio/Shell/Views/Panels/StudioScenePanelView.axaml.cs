using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Editor.Shell.ViewModels.Panels;

namespace Editor.Shell.Views.Panels;

public partial class StudioScenePanelView : UserControl
{
    private const double PickTolerance = 6.0;
    private readonly SceneViewportClickGesture clickGesture_ = new();

    public StudioScenePanelView()
    {
        InitializeComponent();
    }

    private void OnSceneViewportPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var current = e.GetCurrentPoint(SceneViewport);
        var properties = current.Properties;
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
        clickGesture_.Update(e.Pointer.Id, e.GetPosition(SceneViewport));
    }

    private void OnSceneViewportPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
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
        PointerCaptureLostEventArgs e) =>
        clickGesture_.Cancel(e.Pointer.Id);
}

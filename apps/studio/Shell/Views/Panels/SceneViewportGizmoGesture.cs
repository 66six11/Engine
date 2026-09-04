using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;

namespace Editor.Shell.Views.Panels;

internal sealed class SceneViewportGizmoGesture
{
    private int? pointerId_;
    private double renderScaling_;

    public bool TryBegin(
        int pointerId,
        ViewportPresentedInteractionContext context)
    {
        if (pointerId_.HasValue)
        {
            return false;
        }

        pointerId_ = pointerId;
        renderScaling_ = context.RenderScaling;
        return true;
    }

    public bool TryMapPoint(
        int pointerId,
        Point logicalPosition,
        out ViewportPickPoint point)
    {
        point = default;
        if (pointerId_ != pointerId ||
            !double.IsFinite(logicalPosition.X) ||
            !double.IsFinite(logicalPosition.Y))
        {
            return false;
        }

        point = new ViewportPickPoint(
            checked((float)(logicalPosition.X * renderScaling_)),
            checked((float)(logicalPosition.Y * renderScaling_)));
        return true;
    }

    public bool Complete(int pointerId)
    {
        if (pointerId_ != pointerId)
        {
            return false;
        }

        Reset();
        return true;
    }

    public bool Cancel(int pointerId)
    {
        if (pointerId_ != pointerId)
        {
            return false;
        }

        Reset();
        return true;
    }

    public bool Cancel()
    {
        if (!pointerId_.HasValue)
        {
            return false;
        }

        Reset();
        return true;
    }

    private void Reset()
    {
        pointerId_ = null;
        renderScaling_ = 0;
    }
}

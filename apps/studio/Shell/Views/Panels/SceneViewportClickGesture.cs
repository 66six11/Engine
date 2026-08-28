using Avalonia;
using Avalonia.Input;

namespace Editor.Shell.Views.Panels;

internal sealed class SceneViewportClickGesture
{
    internal const double MovementThreshold = 4.0;

    private int? pointerId_;
    private Point pressedAt_;
    private bool movedPastThreshold_;

    public bool TryBegin(
        int pointerId,
        Point position,
        bool isLeftButtonPressed,
        KeyModifiers keyModifiers)
    {
        if (pointerId_.HasValue || !isLeftButtonPressed || keyModifiers != KeyModifiers.None)
        {
            return false;
        }

        pointerId_ = pointerId;
        pressedAt_ = position;
        movedPastThreshold_ = false;
        return true;
    }

    public void Update(int pointerId, Point position)
    {
        if (pointerId_ != pointerId || movedPastThreshold_)
        {
            return;
        }

        var x = position.X - pressedAt_.X;
        var y = position.Y - pressedAt_.Y;
        movedPastThreshold_ = x * x + y * y >
            MovementThreshold * MovementThreshold;
    }

    public bool Complete(
        int pointerId,
        Point position,
        Size viewportSize,
        KeyModifiers keyModifiers)
    {
        if (pointerId_ != pointerId)
        {
            return false;
        }

        Update(pointerId, position);
        var accepted = !movedPastThreshold_ && keyModifiers == KeyModifiers.None &&
            position.X >= 0 && position.Y >= 0 &&
            position.X < viewportSize.Width && position.Y < viewportSize.Height;
        Reset();
        return accepted;
    }

    public void Cancel(int pointerId)
    {
        if (pointerId_ == pointerId)
        {
            Reset();
        }
    }

    private void Reset()
    {
        pointerId_ = null;
        pressedAt_ = default;
        movedPastThreshold_ = false;
    }
}

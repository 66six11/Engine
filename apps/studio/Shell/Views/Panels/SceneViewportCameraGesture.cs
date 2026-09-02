using Asharia.Studio.Application.Viewports;
using Avalonia;
using Avalonia.Input;

namespace Editor.Shell.Views.Panels;

internal sealed class SceneViewportCameraGesture
{
    internal const float WheelDollyFraction = 0.12f;

    private int? pointerId_;
    private Point lastPosition_;
    private ViewportCameraNavigationMode mode_;

    public bool TryBegin(
        int pointerId,
        Point position,
        bool isLeftButtonPressed,
        bool isMiddleButtonPressed,
        bool isRightButtonPressed,
        KeyModifiers keyModifiers)
    {
        if (pointerId_.HasValue || keyModifiers != KeyModifiers.Alt)
        {
            return false;
        }

        var pressedButtonCount = (isLeftButtonPressed ? 1 : 0) +
            (isMiddleButtonPressed ? 1 : 0) +
            (isRightButtonPressed ? 1 : 0);
        if (pressedButtonCount != 1)
        {
            return false;
        }

        mode_ = isLeftButtonPressed
            ? ViewportCameraNavigationMode.Orbit
            : isMiddleButtonPressed
                ? ViewportCameraNavigationMode.Pan
                : ViewportCameraNavigationMode.Dolly;
        pointerId_ = pointerId;
        lastPosition_ = position;
        return true;
    }

    public bool Update(
        int pointerId,
        Point position,
        Size surfaceSize,
        out ViewportCameraNavigationDelta? delta)
    {
        delta = null;
        if (pointerId_ != pointerId)
        {
            return false;
        }

        var horizontal = position.X - lastPosition_.X;
        var vertical = position.Y - lastPosition_.Y;
        lastPosition_ = position;
        if (!TryCreateDelta(
                mode_,
                horizontal,
                vertical,
                surfaceSize,
                out var navigationDelta) ||
            (horizontal == 0 && vertical == 0))
        {
            return true;
        }

        delta = navigationDelta;
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

    public void Cancel(int pointerId)
    {
        if (pointerId_ == pointerId)
        {
            Reset();
        }
    }

    public void Cancel() => Reset();

    public static bool TryCreateWheelDelta(
        double wheelDeltaY,
        Size surfaceSize,
        KeyModifiers keyModifiers,
        out ViewportCameraNavigationDelta delta)
    {
        if (keyModifiers != KeyModifiers.None || !double.IsFinite(wheelDeltaY) ||
            wheelDeltaY == 0)
        {
            delta = default;
            return false;
        }

        return TryCreateDelta(
            ViewportCameraNavigationMode.Dolly,
            horizontal: 0,
            vertical: -wheelDeltaY * surfaceSize.Height * WheelDollyFraction,
            surfaceSize,
            out delta);
    }

    private static bool TryCreateDelta(
        ViewportCameraNavigationMode mode,
        double horizontal,
        double vertical,
        Size surfaceSize,
        out ViewportCameraNavigationDelta delta)
    {
        if (!double.IsFinite(horizontal) || !double.IsFinite(vertical) ||
            !double.IsFinite(surfaceSize.Width) || !double.IsFinite(surfaceSize.Height) ||
            surfaceSize.Width <= 0 || surfaceSize.Height <= 0)
        {
            delta = default;
            return false;
        }

        delta = new ViewportCameraNavigationDelta(
            mode,
            checked((float)(horizontal / surfaceSize.Width)),
            checked((float)(vertical / surfaceSize.Height)),
            checked((float)(surfaceSize.Width / surfaceSize.Height)));
        return true;
    }

    private void Reset()
    {
        pointerId_ = null;
        lastPosition_ = default;
        mode_ = default;
    }
}

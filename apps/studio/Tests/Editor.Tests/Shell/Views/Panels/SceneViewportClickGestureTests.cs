using System;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Input;
using Editor.Shell.Views.Panels;
using Xunit;

namespace Editor.Tests.Shell.Views.Panels;

public sealed class SceneViewportClickGestureTests
{
    [Fact]
    public void Pick_request_maps_logical_pointer_and_tolerance_to_physical_pixels()
    {
        var extent = new ViewportExtent(1200, 900);
        var context = new ViewportPresentedInteractionContext(
            ViewportSessionId.Create(),
            Guid.NewGuid(),
            TargetRevision: 2,
            FrameSequence: 3,
            extent,
            RenderScaling: 1.5);

        var request = StudioScenePanelView.CreatePickRequest(
            context,
            new Point(400, 300));

        Assert.Equal(extent, request.Extent);
        Assert.Equal(600, request.Point.X);
        Assert.Equal(450, request.Point.Y);
        Assert.Equal(9, request.TolerancePixels);
    }

    [Fact]
    public void Primary_unmodified_pointer_completes_inside_the_viewport()
    {
        var gesture = new SceneViewportClickGesture();

        Assert.True(gesture.TryBegin(
            7,
            new Point(10, 10),
            isLeftButtonPressed: true,
            KeyModifiers.None));

        Assert.True(gesture.Complete(
            7,
            new Point(13, 12),
            new Size(100, 80),
            KeyModifiers.None));
    }

    [Theory]
    [InlineData(false, KeyModifiers.None)]
    [InlineData(true, KeyModifiers.Control)]
    [InlineData(true, KeyModifiers.Shift)]
    public void Non_primary_or_modified_press_does_not_begin(
        bool isLeftButtonPressed,
        KeyModifiers modifiers)
    {
        var gesture = new SceneViewportClickGesture();

        Assert.False(gesture.TryBegin(
            1,
            new Point(10, 10),
            isLeftButtonPressed,
            modifiers));
    }

    [Fact]
    public void Movement_past_the_threshold_cancels_the_click()
    {
        var gesture = new SceneViewportClickGesture();
        Assert.True(gesture.TryBegin(
            1,
            new Point(10, 10),
            isLeftButtonPressed: true,
            KeyModifiers.None));

        gesture.Update(1, new Point(15, 10));

        Assert.False(gesture.Complete(
            1,
            new Point(10, 10),
            new Size(100, 80),
            KeyModifiers.None));
    }

    [Theory]
    [InlineData(-1, 10)]
    [InlineData(10, -1)]
    [InlineData(100, 10)]
    [InlineData(10, 80)]
    public void Release_outside_the_viewport_does_not_complete(double x, double y)
    {
        var gesture = new SceneViewportClickGesture();
        Assert.True(gesture.TryBegin(
            1,
            new Point(10, 10),
            isLeftButtonPressed: true,
            KeyModifiers.None));

        Assert.False(gesture.Complete(
            1,
            new Point(x, y),
            new Size(100, 80),
            KeyModifiers.None));
    }

    [Theory]
    [InlineData(true, false, false, ViewportCameraNavigationMode.Orbit)]
    [InlineData(false, true, false, ViewportCameraNavigationMode.Pan)]
    [InlineData(false, false, true, ViewportCameraNavigationMode.Dolly)]
    public void Alt_modified_single_button_begins_camera_navigation(
        bool left,
        bool middle,
        bool right,
        ViewportCameraNavigationMode expectedMode)
    {
        var gesture = new SceneViewportCameraGesture();

        Assert.True(gesture.TryBegin(
            9,
            new Point(100, 60),
            left,
            middle,
            right,
            KeyModifiers.Alt));
        Assert.True(gesture.Update(
            9,
            new Point(120, 45),
            new Size(800, 400),
            out var delta));
        var value = Assert.IsType<ViewportCameraNavigationDelta>(delta);
        Assert.Equal(expectedMode, value.Mode);
        Assert.Equal(0.025f, value.HorizontalFraction);
        Assert.Equal(-0.0375f, value.VerticalFraction);
        Assert.Equal(2.0f, value.AspectRatio);
        Assert.True(gesture.Complete(9));
    }

    [Theory]
    [InlineData(KeyModifiers.None)]
    [InlineData(KeyModifiers.Control | KeyModifiers.Alt)]
    [InlineData(KeyModifiers.Shift | KeyModifiers.Alt)]
    public void Camera_navigation_requires_exact_alt_modifier(KeyModifiers modifiers)
    {
        var gesture = new SceneViewportCameraGesture();

        Assert.False(gesture.TryBegin(
            1,
            new Point(10, 10),
            isLeftButtonPressed: true,
            isMiddleButtonPressed: false,
            isRightButtonPressed: false,
            modifiers));
    }

    [Fact]
    public void Wrong_pointer_and_capture_cancel_do_not_emit_navigation()
    {
        var gesture = new SceneViewportCameraGesture();
        Assert.True(gesture.TryBegin(
            4,
            new Point(10, 10),
            isLeftButtonPressed: true,
            isMiddleButtonPressed: false,
            isRightButtonPressed: false,
            KeyModifiers.Alt));

        Assert.False(gesture.Update(
            5,
            new Point(20, 20),
            new Size(100, 100),
            out _));
        gesture.Cancel(4);
        Assert.False(gesture.Complete(4));
    }

    [Fact]
    public void Focus_cancel_ends_the_active_navigation_gesture()
    {
        var gesture = new SceneViewportCameraGesture();
        Assert.True(gesture.TryBegin(
            4,
            new Point(10, 10),
            isLeftButtonPressed: false,
            isMiddleButtonPressed: true,
            isRightButtonPressed: false,
            KeyModifiers.Alt));

        gesture.Cancel();

        Assert.False(gesture.Update(
            4,
            new Point(20, 20),
            new Size(100, 100),
            out _));
        Assert.False(gesture.Complete(4));
    }

    [Fact]
    public void Relative_drag_is_stable_across_surface_size_and_dpi_scale()
    {
        var logical = new SceneViewportCameraGesture();
        Assert.True(logical.TryBegin(
            1,
            new Point(100, 100),
            isLeftButtonPressed: true,
            isMiddleButtonPressed: false,
            isRightButtonPressed: false,
            KeyModifiers.Alt));
        Assert.True(logical.Update(
            1,
            new Point(140, 120),
            new Size(800, 400),
            out var logicalDelta));

        var scaled = new SceneViewportCameraGesture();
        Assert.True(scaled.TryBegin(
            2,
            new Point(200, 200),
            isLeftButtonPressed: true,
            isMiddleButtonPressed: false,
            isRightButtonPressed: false,
            KeyModifiers.Alt));
        Assert.True(scaled.Update(
            2,
            new Point(280, 240),
            new Size(1600, 800),
            out var scaledDelta));

        Assert.Equal(logicalDelta, scaledDelta);
    }

    [Fact]
    public void Wheel_maps_to_dpi_independent_dolly_fraction()
    {
        Assert.True(SceneViewportCameraGesture.TryCreateWheelDelta(
            wheelDeltaY: 1,
            new Size(800, 400),
            KeyModifiers.None,
            out var delta));

        Assert.Equal(ViewportCameraNavigationMode.Dolly, delta.Mode);
        Assert.Equal(-SceneViewportCameraGesture.WheelDollyFraction, delta.VerticalFraction);
        Assert.Equal(2.0f, delta.AspectRatio);
        Assert.False(SceneViewportCameraGesture.TryCreateWheelDelta(
            wheelDeltaY: 1,
            new Size(1600, 800),
            KeyModifiers.Control,
            out _));
    }
}

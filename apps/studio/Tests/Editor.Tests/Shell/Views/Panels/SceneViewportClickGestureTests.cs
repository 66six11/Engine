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
}

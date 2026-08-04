using Avalonia;
using Editor.Shell.Docking.TabStrips;
using Xunit;

namespace Editor.Tests.Shell.Docking.TabStrips;

public sealed class EditorDockTabStripScrollControllerTests
{
    [Fact]
    public void ClampOffset_limits_value_to_scrollable_range()
    {
        Assert.Equal(0, EditorDockTabStripScrollController.ClampOffset(-20, 500, 200));
        Assert.Equal(120, EditorDockTabStripScrollController.ClampOffset(120, 500, 200));
        Assert.Equal(300, EditorDockTabStripScrollController.ClampOffset(480, 500, 200));
    }

    [Fact]
    public void ClampOffset_returns_zero_when_content_fits()
    {
        Assert.Equal(0, EditorDockTabStripScrollController.ClampOffset(50, 180, 200));
    }

    [Fact]
    public void CalculateOffsetToReveal_scrolls_left_when_target_starts_before_viewport()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(20, 0, 80, 22),
            currentOffset: 120,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(12, offset);
    }

    [Fact]
    public void CalculateOffsetToReveal_scrolls_right_when_target_ends_after_viewport()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(260, 0, 100, 22),
            currentOffset: 40,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(168, offset);
    }

    [Fact]
    public void CalculateOffsetToReveal_keeps_offset_when_target_is_visible()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(120, 0, 50, 22),
            currentOffset: 80,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(80, offset);
    }

    [Fact]
    public void CalculateAutoScrollOffset_moves_near_edges_only_when_overflow_exists()
    {
        Assert.Equal(
            52,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 4,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            148,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 196,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            100,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 100,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            0,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 196,
                currentOffset: 0,
                extentWidth: 180,
                viewportWidth: 200));
    }

    [Fact]
    public void CalculateAutoScrollOffset_prefers_nearest_edge_when_edge_zones_overlap()
    {
        var offset = EditorDockTabStripScrollController.CalculateAutoScrollOffset(
            pointerX: 20,
            currentOffset: 100,
            extentWidth: 600,
            viewportWidth: 30);

        Assert.Equal(148, offset);
    }

    [Fact]
    public void CalculateContentOriginX_offsets_viewport_origin_by_scroll_offset()
    {
        Assert.Equal(
            60,
            EditorDockTabStripScrollController.CalculateContentOriginX(
                viewportOriginX: 260,
                horizontalOffset: 200));
    }
}

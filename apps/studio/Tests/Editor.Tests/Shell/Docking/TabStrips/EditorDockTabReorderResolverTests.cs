using Avalonia;
using Editor.Shell.Docking.TabStrips;
using Xunit;

namespace Editor.Tests.Shell.Docking.TabStrips;

public sealed class EditorDockTabReorderResolverTests
{
    [Fact]
    public void ResolveTargetIndex_moves_local_tab_after_next_tab_when_center_crosses_right_boundary()
    {
        var targetIndex = EditorDockTabReorderResolver.ResolveTargetIndex(
            draggedTabCenterX: 105,
            sourceIndex: 0,
            currentTargetIndex: 0,
            tabCount: 3,
            draggedTabWidth: 100,
            CreateEntries());

        Assert.Equal(2, targetIndex);
    }

    [Fact]
    public void ResolveTargetIndex_keeps_local_tab_before_right_boundary()
    {
        var targetIndex = EditorDockTabReorderResolver.ResolveTargetIndex(
            draggedTabCenterX: 104.9,
            sourceIndex: 0,
            currentTargetIndex: 0,
            tabCount: 3,
            draggedTabWidth: 100,
            CreateEntries());

        Assert.Equal(0, targetIndex);
    }

    [Fact]
    public void ResolveTargetIndex_moves_local_tab_left_after_center_crosses_left_boundary()
    {
        var targetIndex = EditorDockTabReorderResolver.ResolveTargetIndex(
            draggedTabCenterX: 94.9,
            sourceIndex: 0,
            currentTargetIndex: 2,
            tabCount: 3,
            draggedTabWidth: 100,
            CreateEntries());

        Assert.Equal(1, targetIndex);
    }

    [Fact]
    public void ResolveExternalTargetIndex_moves_external_tab_after_first_tab_when_center_crosses_right_boundary()
    {
        var targetIndex = EditorDockTabReorderResolver.ResolveExternalTargetIndex(
            draggedTabCenterX: 105,
            currentTargetIndex: 0,
            tabCount: 3,
            draggedTabWidth: 100,
            CreateEntries());

        Assert.Equal(1, targetIndex);
    }

    [Fact]
    public void ResolveExternalTargetIndex_keeps_current_preview_until_left_boundary_is_crossed()
    {
        var targetIndex = EditorDockTabReorderResolver.ResolveExternalTargetIndex(
            draggedTabCenterX: 196,
            currentTargetIndex: 2,
            tabCount: 3,
            draggedTabWidth: 100,
            CreateEntries());

        Assert.Equal(2, targetIndex);
    }

    [Theory]
    [InlineData(856.5, 0, 0)]
    [InlineData(856.5, 1, 1)]
    [InlineData(862.49, 0, 0)]
    [InlineData(862.5, 0, 1)]
    [InlineData(852.5, 1, 1)]
    [InlineData(852.49, 1, 0)]
    public void ResolveExternalTargetIndex_uses_one_boundary_for_captured_unequal_width_pair(
        double center, int current, int expected)
    {
        EditorDockTabReorderResolver.Entry[] entries = [new(0, new Rect(743, 771, 128, 22))];
        var target = EditorDockTabReorderResolver.ResolveExternalTargetIndex(
            center, current, 1, 101, entries);

        Assert.Equal(expected, target);
        Assert.Equal(target, EditorDockTabReorderResolver.ResolveExternalTargetIndex(
            center, target, 1, 101, entries));
    }

    [Theory]
    [InlineData(101, 128)]
    [InlineData(128, 101)]
    [InlineData(20, 300)]
    [InlineData(300, 20)]
    public void ResolveExternalTargetIndex_remains_stable_across_multiple_unequal_tabs(
        double draggedWidth, double targetWidth)
    {
        EditorDockTabReorderResolver.Entry[] entries =
        [
            new(0, new Rect(-70, 0, targetWidth, 22)),
            new(1, new Rect(-70 + targetWidth, 0, 47, 22)),
            new(2, new Rect(-23 + targetWidth, 0, 215, 22)),
        ];
        for (var current = 0; current <= entries.Length; current++)
        {
            for (double center = -100; center < 800; center += 2.5)
            {
                var target = EditorDockTabReorderResolver.ResolveExternalTargetIndex(
                    center, current, entries.Length, draggedWidth, entries);
                Assert.InRange(target, 0, entries.Length);
                Assert.Equal(target, EditorDockTabReorderResolver.ResolveExternalTargetIndex(
                    center, target, entries.Length, draggedWidth, entries));
            }
        }
    }

    [Theory]
    [InlineData(101, 128)]
    [InlineData(128, 101)]
    [InlineData(20, 300)]
    [InlineData(300, 20)]
    public void ResolveTargetIndex_keeps_local_unequal_width_moves_stable(
        double sourceWidth, double otherWidth)
    {
        EditorDockTabReorderResolver.Entry[] entries =
        [
            new(0, new Rect(0, 0, sourceWidth, 22)),
            new(1, new Rect(sourceWidth, 0, otherWidth, 22)),
            new(2, new Rect(sourceWidth + otherWidth, 0, 47, 22)),
        ];
        for (var source = 0; source < entries.Length; source++)
        {
            for (var current = 0; current <= entries.Length; current++)
            {
                for (double center = -10; center < 700; center += 2.5)
                {
                    var target = EditorDockTabReorderResolver.ResolveTargetIndex(
                        center, source, current, entries.Length, sourceWidth, entries);
                    Assert.InRange(target, 0, entries.Length);
                    Assert.Equal(target, EditorDockTabReorderResolver.ResolveTargetIndex(
                        center, source, target, entries.Length, sourceWidth, entries));
                }
            }
        }
    }

    private static EditorDockTabReorderResolver.Entry[] CreateEntries()
    {
        return
        [
            new EditorDockTabReorderResolver.Entry(0, new Rect(0, 0, 100, 32)),
            new EditorDockTabReorderResolver.Entry(1, new Rect(100, 0, 100, 32)),
            new EditorDockTabReorderResolver.Entry(2, new Rect(200, 0, 100, 32)),
        ];
    }
}

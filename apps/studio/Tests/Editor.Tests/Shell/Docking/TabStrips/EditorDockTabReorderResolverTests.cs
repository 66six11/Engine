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

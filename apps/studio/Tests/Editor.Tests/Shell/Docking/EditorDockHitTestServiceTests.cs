using Avalonia;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Shell.Docking;

public sealed class EditorDockHitTestServiceTests
{
    [Fact]
    public void HitTest_inserts_external_tab_after_first_tab_when_center_crosses_right_boundary()
    {
        var target = HitTestTabWell(
            CreateWindow(),
            tabInsertProbeX: 105,
            tabInsertCurrentTargetIndex: 0);

        Assert.Equal(EditorDockDropOperation.InsertTabAtIndex, target.Operation);
        Assert.Equal(1, target.TargetIndex);
    }

    [Fact]
    public void HitTest_keeps_external_preview_index_inside_current_hysteresis_band()
    {
        var target = HitTestTabWell(
            CreateWindow(),
            tabInsertProbeX: 196,
            tabInsertCurrentTargetIndex: 2);

        Assert.Equal(EditorDockDropOperation.InsertTabAtIndex, target.Operation);
        Assert.Equal(2, target.TargetIndex);
    }

    [Fact]
    public void HitTest_ignores_existing_placeholder_visual_offset_when_resolving_tab_index()
    {
        var window = CreateWindow(
            new EditorDockTabBounds("tab-0", 0, new Rect(100, 0, 100, 32), IsDragSource: false),
            new EditorDockTabBounds("tab-1", 1, new Rect(200, 0, 100, 32), IsDragSource: false),
            new EditorDockTabBounds("tab-2", 2, new Rect(300, 0, 100, 32), IsDragSource: false));

        var target = HitTestTabWell(
            window,
            tabInsertProbeX: 105,
            tabInsertCurrentTargetIndex: 0);

        Assert.Equal(EditorDockDropOperation.InsertTabAtIndex, target.Operation);
        Assert.Equal(1, target.TargetIndex);
    }

    [Fact]
    public void HitTest_uses_reorder_resolver_for_source_window_tab_insert()
    {
        var window = CreateWindow(
            [
                new EditorDockTabBounds("tab-0", 0, new Rect(0, 0, 100, 32), IsDragSource: true),
                new EditorDockTabBounds("tab-1", 1, new Rect(100, 0, 100, 32), IsDragSource: false),
                new EditorDockTabBounds("tab-2", 2, new Rect(200, 0, 100, 32), IsDragSource: false),
            ],
            dragSourceTabIndex: 0,
            isDragSource: true);

        var target = HitTestTabWell(
            window,
            tabInsertProbeX: 105,
            tabInsertCurrentTargetIndex: 0);

        Assert.Equal(EditorDockDropOperation.InsertTabAtIndex, target.Operation);
        Assert.Equal(2, target.TargetIndex);
    }

    private static EditorDockDropTarget HitTestTabWell(
        EditorDockWindowBounds window,
        double tabInsertProbeX,
        int tabInsertCurrentTargetIndex)
    {
        return EditorDockHitTestService.HitTest(
            pointer: new Point(10, 10),
            workspaceBounds: new Rect(0, 0, 500, 300),
            windows: [window],
            splitters: [],
            tabInsertProbeX: tabInsertProbeX,
            tabInsertPreviewWindowId: window.WindowId,
            tabInsertCurrentTargetIndex: tabInsertCurrentTargetIndex,
            tabInsertDraggedTabWidth: 100);
    }

    private static EditorDockWindowBounds CreateWindow(
        params EditorDockTabBounds[] tabBounds)
    {
        return CreateWindow(tabBounds, dragSourceTabIndex: null, isDragSource: false);
    }

    private static EditorDockWindowBounds CreateWindow(
        EditorDockTabBounds[] tabBounds,
        int? dragSourceTabIndex,
        bool isDragSource)
    {
        var tabs = tabBounds.Length > 0
            ? tabBounds
            :
            [
                new EditorDockTabBounds("tab-0", 0, new Rect(0, 0, 100, 32), IsDragSource: false),
                new EditorDockTabBounds("tab-1", 1, new Rect(100, 0, 100, 32), IsDragSource: false),
                new EditorDockTabBounds("tab-2", 2, new Rect(200, 0, 100, 32), IsDragSource: false),
            ];

        return new EditorDockWindowBounds(
            WindowId: "window",
            Area: DockArea.Center,
            Bounds: new Rect(0, 0, 400, 200),
            TabWellBounds: new Rect(0, 0, 320, 32),
            TabCount: tabs.Length,
            TabBounds: tabs,
            DragSourceTabIndex: dragSourceTabIndex,
            AllowsWindowInsertion: true,
            IsDragSource: isDragSource);
    }
}

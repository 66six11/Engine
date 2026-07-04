using Editor.Core.Models.Panels;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Docking;

public sealed class EditorDockWindowViewModelTests
{
    [Fact]
    public void Remove_clears_removed_active_tab_state()
    {
        var window = new EditorDockWindowViewModel("window", "Window", DockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        window.Add(first);
        window.Add(second);

        window.Remove(first);

        Assert.False(first.IsActive);
        Assert.False(first.IsDragSource);
        Assert.Same(second, window.ActiveTab);
        Assert.True(second.IsActive);
    }

    [Fact]
    public void Host_focus_controls_active_tab_focus_indicator()
    {
        var window = new EditorDockWindowViewModel("window", "Window", DockArea.Center, "Test");
        var tab = CreateTab("tab");
        window.Add(tab);
        window.SetActiveWindowState(true);
        var tabStripItem = window.TabStripItems[0];

        Assert.True(tabStripItem.IsSelectedInFocusedWindow);
        Assert.False(tabStripItem.IsSelectedInInactiveWindow);

        window.SetHostFocusState(false);

        Assert.False(tabStripItem.IsSelectedInFocusedWindow);
        Assert.True(tabStripItem.IsSelectedInInactiveWindow);

        window.SetHostFocusState(true);

        Assert.True(tabStripItem.IsSelectedInFocusedWindow);
        Assert.False(tabStripItem.IsSelectedInInactiveWindow);
    }

    [Fact]
    public void HideDragSourceTab_collapses_source_tab_until_cleared()
    {
        var window = new EditorDockWindowViewModel("window", "Window", DockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        window.Add(first);
        window.Add(second);

        Assert.True(window.HideDragSourceTab(first));

        var visibleItem = Assert.Single(window.TabStripItems);
        Assert.Same(second, visibleItem.Tab);

        Assert.True(window.ClearHiddenDragSourceTab());

        Assert.Equal(2, window.TabStripItems.Count);
        Assert.Same(first, window.TabStripItems[0].Tab);
        Assert.Same(second, window.TabStripItems[1].Tab);
        Assert.False(window.TabStripItems[0].IsSourceGhost);
        Assert.False(window.TabStripItems[1].IsSourceGhost);
    }

    [Fact]
    public void ShowLocalTabReorderPreview_collapses_source_tab_and_inserts_placeholder()
    {
        var window = new EditorDockWindowViewModel("window", "Window", DockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        var third = CreateTab("third");
        window.Add(first);
        window.Add(second);
        window.Add(third);

        Assert.True(window.ShowLocalTabReorderPreview(first, 2, showsTab: false));
        Assert.False(window.ShowLocalTabReorderPreview(first, 2, showsTab: false));

        Assert.Equal(3, window.TabStripItems.Count);
        Assert.Same(second, window.TabStripItems[0].Tab);
        Assert.Same(first, window.TabStripItems[1].Tab);
        Assert.True(window.TabStripItems[1].IsPlaceholder);
        Assert.Same(third, window.TabStripItems[2].Tab);

        Assert.True(window.ClearLocalTabReorderPreview());

        Assert.Equal(3, window.TabStripItems.Count);
        Assert.Same(first, window.TabStripItems[0].Tab);
        Assert.Same(second, window.TabStripItems[1].Tab);
        Assert.Same(third, window.TabStripItems[2].Tab);
        Assert.All(window.TabStripItems, item => Assert.False(item.IsPlaceholder));
    }

    private static EditorDockTabViewModel CreateTab(string id)
    {
        return new EditorDockTabViewModel(
            id,
            id,
            "TEST",
            id,
            "idle",
            PanelKind.Tool,
            DockArea.Center,
            new object());
    }
}

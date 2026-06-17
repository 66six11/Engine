using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

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

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

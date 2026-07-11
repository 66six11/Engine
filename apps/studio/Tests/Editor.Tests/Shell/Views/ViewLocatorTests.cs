using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Editor.UI.CodeFirst.Authoring;
using Editor.Shell.CodeFirstUI.Hosting;
using Editor.Shell.CodeFirstUI.Views;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class ViewLocatorTests
{
    [Fact]
    public void Build_maps_code_first_panel_host_to_view()
    {
        var locator = new ViewLocator();

        var view = locator.Build(new CodeFirstPanelHostViewModel(new EmptyCodeFirstPanel()));

        Assert.IsType<CodeFirstPanelHostView>(view);
    }

    private sealed class EmptyCodeFirstPanel : CodeFirstEditorPanel
    {
        protected override void OnGui(EditorGui gui)
        {
            gui.Text("empty", "Empty");
        }
    }
}

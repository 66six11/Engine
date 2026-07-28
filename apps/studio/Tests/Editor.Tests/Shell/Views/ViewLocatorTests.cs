using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Editor.UI.CodeFirst.Authoring;
using Editor.Shell.CodeFirstUI.Hosting;
using Editor.Shell.CodeFirstUI.Views;
using Editor.Features.Project.ViewModels;
using Editor.Features.Project.Views;
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

    [Fact]
    public void Build_maps_project_panel_to_compiled_xaml_view()
    {
        var locator = new ViewLocator();

        var view = locator.Build(new ProjectPanelViewModel());

        Assert.IsType<ProjectPanelView>(view);
    }

    private sealed class EmptyCodeFirstPanel : CodeFirstEditorPanel
    {
        protected override void OnGui(EditorGui gui)
        {
            gui.Text("empty", "Empty");
        }
    }
}

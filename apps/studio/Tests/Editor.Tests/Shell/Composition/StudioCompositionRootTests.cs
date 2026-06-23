using System.Linq;
using System.Threading.Tasks;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioCompositionRootTests
{
    [Fact]
    public void CreateDefaultComposition_declares_modules_once_for_panel_and_action_registries()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
            ],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public async Task CreateMainWindowViewModel_uses_shared_default_composition()
    {
        await using var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);
        var viewModel = session.MainWindowViewModel;

        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
    }

    [Fact]
    public async Task CreateMainWindowSession_keeps_extension_host_alive_until_session_disposal()
    {
        var session = new StudioCompositionRoot().CreateMainWindowSession(savedLayout: null);

        Assert.NotEmpty(session.Composition.PanelRegistry.GetAll());
        Assert.NotEmpty(session.Composition.ActionRegistry.GetAll());

        await session.DisposeAsync();

        Assert.Empty(session.Composition.PanelRegistry.GetAll());
        Assert.Empty(session.Composition.ActionRegistry.GetAll());
    }
}

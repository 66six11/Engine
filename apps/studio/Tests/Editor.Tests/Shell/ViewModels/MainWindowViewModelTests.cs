using System.Linq;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class MainWindowViewModelTests
{
    [Fact]
    public void CreatePanelRegistry_uses_feature_module_panel_content()
    {
        var registry = MainWindowViewModel.CreatePanelRegistry();

        Assert.IsType<HierarchyPanelViewModel>(
            registry.GetRequired("hierarchy").CreateContent());
        Assert.IsType<InspectorPanelViewModel>(
            registry.GetRequired("inspector").CreateContent());
    }

    [Fact]
    public void PanelMenuItems_follow_registered_panel_descriptors()
    {
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            savedLayout: null);

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            viewModel.PanelMenuItems.Select(item => item.PanelId));
        Assert.Equal(
            ["Scene View", "Hierarchy", "Inspector", "Console", "Problems"],
            viewModel.PanelMenuItems.Select(item => item.Header));
    }

    [Fact]
    public void OpenPanelCommand_opens_feature_panel_content()
    {
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            savedLayout: null);
        var hierarchy = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");

        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchy));

        viewModel.OpenPanelCommand.Execute("hierarchy");

        var reopened = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        Assert.IsType<HierarchyPanelViewModel>(reopened.Content);
    }

    [Fact]
    public void RestoreLayoutSnapshot_restores_feature_panel_by_id()
    {
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            savedLayout: null);

        var restored = viewModel.DockWorkspace.RestoreLayoutSnapshot(new EditorDockLayoutSnapshot
        {
            Version = 1,
            ActiveWindowId = "restored-inspector",
            Root = new EditorDockLayoutNodeSnapshot
            {
                Kind = "Window",
                Id = "restored-node",
                WindowId = "restored-inspector",
                WindowTitle = "Inspector",
                WindowArea = Core.Models.DockArea.Right,
                WindowRole = "Selection context",
                TabIds = ["inspector"],
                ActiveTabId = "inspector",
            },
        });

        Assert.True(restored);
        var activeWindow = Assert.IsType<EditorDockWindowViewModel>(viewModel.DockWorkspace.ActiveWindow);
        var tab = Assert.Single(activeWindow.Tabs);
        Assert.Equal("inspector", tab.Id);
        Assert.IsType<InspectorPanelViewModel>(tab.Content);
    }
}

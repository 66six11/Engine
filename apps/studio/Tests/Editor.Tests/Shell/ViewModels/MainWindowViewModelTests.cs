using System.Linq;
using Avalonia.Input;
using Editor.Core.Models;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Editor.Shell.Selection;
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
    public void Default_panel_content_shares_main_window_selection_service()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(selectionService),
            MainWindowViewModel.CreateWorkbenchActionRegistry(selectionService),
            savedLayout: null,
            selectionService);
        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Same(selectionService, viewModel.SelectionService);
        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
    }

    [Fact]
    public void PanelMenuItems_follow_registered_workbench_actions()
    {
        var viewModel = CreateMainWindowViewModel();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            viewModel.PanelMenuItems.Select(item => item.PanelId));
        Assert.Equal(
            ["Scene View", "Hierarchy", "Inspector", "Console", "Problems"],
            viewModel.PanelMenuItems.Select(item => item.Header));
    }

    [Fact]
    public void ToolsMenuItems_follow_registered_workbench_actions()
    {
        var viewModel = CreateMainWindowViewModel();

        var item = Assert.Single(viewModel.ToolsMenuItems);
        Assert.Equal("workbench.commandPalette.open", item.CommandId);
        Assert.Equal("Command Palette", item.Header);
        Assert.Equal("Tools/Command Palette", item.MenuPath);
        Assert.Equal("Ctrl+Shift+P", item.ShortcutText);
    }

    [Fact]
    public void ToolsMenuItems_open_command_palette_through_command_route()
    {
        var viewModel = CreateMainWindowViewModel();
        var item = Assert.Single(viewModel.ToolsMenuItems);

        item.OpenCommand.Execute(null);

        Assert.True(viewModel.CommandPalette.IsOpen);
    }

    [Fact]
    public void PanelMenuItems_use_action_registry_instead_of_panel_descriptor_menu_data()
    {
        var actions = new WorkbenchActionRegistry();
        actions.Register(new WorkbenchActionDescriptor(
            "test.open.problems",
            "Validation",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Validation",
            TargetId: "problems",
            IconKey: "studio.problems"));
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            actions,
            savedLayout: null);

        var item = Assert.Single(viewModel.PanelMenuItems);
        Assert.Equal("problems", item.PanelId);
        Assert.Equal("Validation", item.Header);
        Assert.Equal("studio.problems", item.IconKey);
    }

    [Fact]
    public void OpenPanelCommand_opens_feature_panel_content()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchy = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");

        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchy));

        viewModel.OpenPanelCommand.Execute("hierarchy");

        var reopened = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        Assert.IsType<HierarchyPanelViewModel>(reopened.Content);
    }

    [Fact]
    public void CommandPalette_executes_panel_actions_through_panel_command_route()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchy = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchy));

        viewModel.CommandPalette.OpenCommand.Execute(null);
        viewModel.CommandPalette.Query = "hierarchy";
        viewModel.CommandPalette.ExecuteSelectedCommand.Execute(null);

        var reopened = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        Assert.IsType<HierarchyPanelViewModel>(reopened.Content);
        Assert.False(viewModel.CommandPalette.IsOpen);
    }

    [Fact]
    public void ExecuteShortcut_opens_command_palette_through_registered_shortcut()
    {
        var viewModel = CreateMainWindowViewModel();

        var result = viewModel.ExecuteShortcut(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: false);

        Assert.NotNull(result);
        Assert.True(result.Succeeded);
        Assert.True(viewModel.CommandPalette.IsOpen);
    }

    [Fact]
    public void PanelMenuItems_reflect_open_panels_in_main_workspace()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchyItem = viewModel.PanelMenuItems.Single(item => item.PanelId == "hierarchy");
        var hierarchyTab = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");

        Assert.True(hierarchyItem.IsOpen);

        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchyTab));

        Assert.False(hierarchyItem.IsOpen);

        hierarchyItem.OpenCommand.Execute(null);

        Assert.True(hierarchyItem.IsOpen);
    }

    [Fact]
    public void PanelMenuItems_include_open_panels_from_floating_windows()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchyItem = viewModel.PanelMenuItems.Single(item => item.PanelId == "hierarchy");
        var hierarchyTab = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        var floatingPanels = new FloatingPanelOpenState("hierarchy");

        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchyTab));
        Assert.False(hierarchyItem.IsOpen);

        viewModel.SetFloatingWindowCallbacks(
            () => [],
            () => { },
            _ => false,
            floatingPanels.ContainsPanel);

        Assert.True(hierarchyItem.IsOpen);

        floatingPanels.Close();
        viewModel.RefreshPanelMenuOpenStates();

        Assert.False(hierarchyItem.IsOpen);
    }

    [Fact]
    public void PanelMenuItems_open_command_focuses_floating_panel_before_reopening_main_panel()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchyItem = viewModel.PanelMenuItems.Single(item => item.PanelId == "hierarchy");
        var hierarchyTab = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        var focusCount = 0;
        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchyTab));
        viewModel.SetFloatingWindowCallbacks(
            () => [],
            () => { },
            panelId =>
            {
                focusCount++;
                return panelId == "hierarchy";
            },
            panelId => panelId == "hierarchy");

        hierarchyItem.OpenCommand.Execute(null);

        Assert.Equal(1, focusCount);
        Assert.False(viewModel.DockWorkspace.ContainsPanel("hierarchy"));
    }

    [Fact]
    public void RestoreLayoutSnapshot_restores_feature_panel_by_id()
    {
        var viewModel = CreateMainWindowViewModel();

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

    private static MainWindowViewModel CreateMainWindowViewModel()
    {
        return new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            MainWindowViewModel.CreateWorkbenchActionRegistry(),
            savedLayout: null);
    }

    private sealed class FloatingPanelOpenState(string openPanelId)
    {
        private bool isOpen_ = true;

        public bool ContainsPanel(string panelId)
        {
            return isOpen_ && panelId == openPanelId;
        }

        public void Close()
        {
            isOpen_ = false;
        }
    }
}

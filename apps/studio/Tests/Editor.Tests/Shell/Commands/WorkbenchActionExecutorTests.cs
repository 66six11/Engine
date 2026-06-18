using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchActionExecutorTests
{
    [Fact]
    public void Execute_open_panel_action_opens_registered_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var executor = new WorkbenchActionExecutor(new PanelCommandService(workspace));
        Assert.True(workspace.CloseTab(tab));

        Assert.True(executor.Execute(CreateOpenPanelAction("panel")));

        Assert.True(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_open_panel_action_uses_panel_command_route()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var panelCommandService = new PanelCommandService(workspace);
        var executor = new WorkbenchActionExecutor(panelCommandService);
        var externalFocusCount = 0;
        Assert.True(workspace.CloseTab(tab));
        panelCommandService.SetExternalPanelCallbacks(
            panelId =>
            {
                externalFocusCount++;
                return panelId == "panel";
            },
            panelId => panelId == "panel");

        Assert.True(executor.Execute(CreateOpenPanelAction("panel")));

        Assert.Equal(1, externalFocusCount);
        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_open_panel_action_without_target_returns_false()
    {
        var workspace = CreateWorkspace();
        var executor = new WorkbenchActionExecutor(new PanelCommandService(workspace));
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.missing-target",
            "Missing Target",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Missing Target");

        Assert.False(executor.Execute(action));
    }

    [Fact]
    public void Execute_unknown_action_kind_returns_false()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var executor = new WorkbenchActionExecutor(new PanelCommandService(workspace));
        var action = new WorkbenchActionDescriptor(
            "workbench.unknown.panel",
            "Unknown",
            (WorkbenchActionKind)999,
            "Window/Panels/Unknown",
            TargetId: "panel");
        Assert.True(workspace.CloseTab(tab));

        Assert.False(executor.Execute(action));

        Assert.False(workspace.ContainsPanel("panel"));
    }

    private static WorkbenchActionDescriptor CreateOpenPanelAction(string panelId)
    {
        return new WorkbenchActionDescriptor(
            $"workbench.panel.{panelId}",
            "Panel",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Panel",
            TargetId: panelId);
    }

    private static EditorDockWorkspaceViewModel CreateWorkspace()
    {
        var registry = new PanelRegistry();
        registry.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            DockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        return new EditorDockWorkspaceViewModel(registry);
    }
}

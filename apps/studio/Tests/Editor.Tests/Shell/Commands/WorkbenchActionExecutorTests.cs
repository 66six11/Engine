using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Panels;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Workbench;
using Editor.Shell.Commands;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchActionExecutorTests
{
    [Fact]
    public void Execute_open_panel_action_opens_registered_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var action = CreateOpenPanelAction("panel");
        var executor = CreateBuiltInExecutor(workspace, action);
        Assert.True(workspace.CloseTab(tab));

        Assert.True(executor.Execute(action));

        Assert.True(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_open_panel_action_uses_panel_command_route()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var panelCommandService = new PanelCommandService(workspace);
        var action = CreateOpenPanelAction("panel");
        var executor = CreateBuiltInExecutor(panelCommandService, action);
        var externalFocusCount = 0;
        Assert.True(workspace.CloseTab(tab));
        panelCommandService.SetExternalPanelCallbacks(
            panelId =>
            {
                externalFocusCount++;
                return panelId == "panel";
            },
            panelId => panelId == "panel");

        Assert.True(executor.Execute(action));

        Assert.Equal(1, externalFocusCount);
        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_disabled_action_returns_false_without_opening_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        Assert.True(workspace.CloseTab(tab));
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.disabled",
            "Disabled",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Disabled",
            TargetId: "panel",
            Category: "Window",
            IsEnabled: false,
            DisabledReason: "Disabled by test");
        var executor = CreateBuiltInExecutor(workspace, action);

        Assert.False(executor.Execute(action));

        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_open_command_palette_action_invokes_callback()
    {
        var openCount = 0;
        var action = new WorkbenchActionDescriptor(
            "workbench.commandPalette.open",
            "Command Palette",
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Command Palette");
        var executor = CreateBuiltInExecutor(
            CreateWorkspace(),
            action,
            () =>
            {
                openCount++;
                return true;
            });

        Assert.True(executor.Execute(action));
        Assert.Equal(1, openCount);
    }

    [Fact]
    public void Execute_open_about_dialog_action_invokes_callback()
    {
        var openCount = 0;
        var action = new WorkbenchActionDescriptor(
            "workbench.about.open",
            "About",
            WorkbenchActionKind.OpenAboutDialog,
            "Help/About");
        var executor = CreateBuiltInExecutor(
            CreateWorkspace(),
            action,
            openCommandPalette: null,
            openAboutDialog: () =>
            {
                openCount++;
                return true;
            });

        Assert.True(executor.Execute(action));
        Assert.Equal(1, openCount);
    }

    [Fact]
    public void Execute_open_panel_action_without_target_returns_false()
    {
        var workspace = CreateWorkspace();
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.missing-target",
            "Missing Target",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Missing Target");
        var executor = CreateBuiltInExecutor(workspace, action);

        Assert.False(executor.Execute(action));
    }

    [Fact]
    public void Execute_unknown_action_kind_returns_false()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var action = new WorkbenchActionDescriptor(
            "workbench.unknown.panel",
            "Unknown",
            (WorkbenchActionKind)999,
            "Window/Panels/Unknown",
            TargetId: "panel");
        var executor = CreateBuiltInExecutor(workspace, action);
        Assert.True(workspace.CloseTab(tab));

        Assert.False(executor.Execute(action));

        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void Execute_registered_command_handler_uses_action_id_without_matching_action_kind()
    {
        var executedActionIds = new List<string>();
        var handlers = new WorkbenchCommandHandlerRegistry();
        handlers.Register(
            "project.command.run-tool",
            action =>
            {
                executedActionIds.Add(action.Id);
                return true;
            });
        var executor = new WorkbenchActionExecutor(handlers);
        var action = new WorkbenchActionDescriptor(
            "project.command.run-tool",
            "Run Tool",
            (WorkbenchActionKind)999,
            "Tools/Run Tool");

        Assert.True(executor.Execute(action));

        Assert.Equal(["project.command.run-tool"], executedActionIds);
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

    private static WorkbenchActionExecutor CreateBuiltInExecutor(
        EditorDockWorkspaceViewModel workspace,
        WorkbenchActionDescriptor action,
        Func<bool>? openCommandPalette = null,
        Func<bool>? openAboutDialog = null)
    {
        return CreateBuiltInExecutor(
            new PanelCommandService(workspace),
            action,
            openCommandPalette,
            openAboutDialog);
    }

    private static WorkbenchActionExecutor CreateBuiltInExecutor(
        PanelCommandService panelCommandService,
        WorkbenchActionDescriptor action,
        Func<bool>? openCommandPalette = null,
        Func<bool>? openAboutDialog = null)
    {
        var handlers = WorkbenchCommandHandlerRegistry.CreateBuiltIn(
            [action],
            panelCommandService,
            openCommandPalette,
            openAboutDialog);
        return new WorkbenchActionExecutor(handlers);
    }

    private static EditorDockWorkspaceViewModel CreateWorkspace()
    {
        var registry = new PanelRegistry();
        registry.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        return new EditorDockWorkspaceViewModel(registry);
    }
}

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Panels;
using Asharia.Editor.Projects;
using Asharia.Editor.Selection;
using Asharia.Editor.Tasks;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Workbench;
using Editor.Core.Services;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Asharia.Studio.Application.Lifecycle;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Tasks;
using Editor.Shell.Services;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Windowing;

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
        var composition = CreateDefaultComposition(selectionService);
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            selectionService,
            uiDispatcher: new CapturingUiDispatcher(hasAccess: true));
        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Same(selectionService, viewModel.SelectionService);
        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
        Assert.Equal("Demo Cube", viewModel.SelectionSummary);
    }

    [Fact]
    public void Workbench_context_uses_explicit_placeholders_and_tracks_shared_selection()
    {
        var selectionService = new EditorSelectionService();
        var composition = CreateDefaultComposition(selectionService);
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            selectionService,
            uiDispatcher: new CapturingUiDispatcher(hasAccess: true),
            defaultLayoutFactory: EditorWorkbenchLayoutPreset.CreateDefault);

        Assert.Equal("No active project", viewModel.ActiveProjectDisplayName);
        Assert.Equal("No document", viewModel.DocumentDisplayName);
        Assert.False(viewModel.IsDocumentDirty);
        Assert.Equal(
            "No document — No active project — Asharia Studio",
            viewModel.WindowTitle);
        Assert.Equal("Edit", viewModel.EditorModeText);
        Assert.Equal("Nothing selected", viewModel.SelectionSummary);
        Assert.Equal("No active tasks", viewModel.BackgroundTaskSummary);
        Assert.Equal("No diagnostics", viewModel.DiagnosticSummary);

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:cube", "mesh", "Demo Cube")]);

        Assert.Equal("Demo Cube", viewModel.SelectionSummary);
    }

    [Fact]
    public void Workbench_context_selection_updates_on_ui_dispatcher()
    {
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var selectionService = new EditorSelectionService();
        var composition = CreateDefaultComposition(selectionService);
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            selectionService,
            uiDispatcher: dispatcher,
            defaultLayoutFactory: EditorWorkbenchLayoutPreset.CreateDefault);

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:cube", "mesh", "Demo Cube")]);

        Assert.Equal("Nothing selected", viewModel.SelectionSummary);
        dispatcher.RunPostedActions();
        Assert.Equal("Demo Cube", viewModel.SelectionSummary);
    }

    [Fact]
    public void Project_open_context_updates_on_ui_dispatcher()
    {
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var projectOpenSessions = new ProjectOpenSessionSnapshotSource();
        using var viewModel = CreateMainWindowViewModel(
            uiDispatcher: dispatcher,
            projectOpenSessions: projectOpenSessions);

        projectOpenSessions.Publish(CreateReadyProjectOpenSnapshot());

        Assert.Equal("No project", viewModel.ProjectLaunch.ProjectCandidateDisplayName);
        Assert.Equal(1, dispatcher.PostCount);
        dispatcher.RunPostedActions();
        Assert.Equal("Example", viewModel.ProjectLaunch.ProjectCandidateDisplayName);
        Assert.Equal("Project check completed", viewModel.ProjectLaunch.StateTitle);
        Assert.Equal("No active project", viewModel.ActiveProjectDisplayName);
        Assert.Equal(
            "No document — No active project — Asharia Studio",
            viewModel.WindowTitle);
        Assert.Contains(
            "project is active",
            viewModel.SessionUnavailableReason,
            StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Active_project_session_updates_workbench_context_on_ui_dispatcher()
    {
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var projectSessions = new StubProjectSessionService();
        using var viewModel = CreateMainWindowViewModel(
            uiDispatcher: dispatcher,
            projectSessions: projectSessions);

        projectSessions.Publish(ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                @"D:\Projects\Example",
                "Example",
                Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));

        Assert.Equal("No active project", viewModel.ActiveProjectDisplayName);
        Assert.Equal(1, dispatcher.PostCount);

        dispatcher.RunPostedActions();

        Assert.True(viewModel.HasActiveProject);
        Assert.Equal("Example", viewModel.ActiveProjectDisplayName);
        Assert.Equal("Untitled Scene", viewModel.DocumentDisplayName);
        Assert.Equal(
            "Untitled Scene — Example — Asharia Studio",
            viewModel.WindowTitle);
        Assert.Contains(
            "runtime session",
            viewModel.SessionUnavailableReason,
            StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Project_actions_forward_selected_roots_and_publish_status()
    {
        var projectSessions = new StubProjectSessionService();
        using var viewModel = CreateMainWindowViewModel(
            projectSessions: projectSessions);

        var createResult = viewModel.CreateMinimalProject(
            @"D:\Projects\Example");
        var openResult = viewModel.OpenProject(
            @"D:\Projects\Existing");

        Assert.True(createResult.Succeeded);
        Assert.Equal(@"D:\Projects\Example", projectSessions.CreatedRoot);
        Assert.Equal("Example", projectSessions.CreatedName);
        Assert.True(openResult.Succeeded);
        Assert.Equal(@"D:\Projects\Existing", projectSessions.OpenedRoot);
        Assert.Equal(
            EditorStatusMessageSeverity.Success,
            viewModel.LastStatusMessage?.Severity);
        Assert.Equal("Opened project 'Existing'.", viewModel.StatusMessageText);
    }

    [Fact]
    public void Dispose_unsubscribes_from_project_open_context()
    {
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var projectOpenSessions = new ProjectOpenSessionSnapshotSource();
        var viewModel = CreateMainWindowViewModel(
            uiDispatcher: dispatcher,
            projectOpenSessions: projectOpenSessions);

        viewModel.Dispose();
        projectOpenSessions.Publish(CreateReadyProjectOpenSnapshot());

        Assert.Equal(0, dispatcher.PostCount);
        Assert.Equal("No project", viewModel.ProjectLaunch.ProjectCandidateDisplayName);
    }

    [Fact]
    public void Dispose_unsubscribes_from_active_project_session()
    {
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var projectSessions = new StubProjectSessionService();
        var viewModel = CreateMainWindowViewModel(
            uiDispatcher: dispatcher,
            projectSessions: projectSessions);

        viewModel.Dispose();
        projectSessions.Publish(ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                @"D:\Projects\Example",
                "Example",
                Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));

        Assert.Equal(0, dispatcher.PostCount);
        Assert.Equal("No active project", viewModel.ActiveProjectDisplayName);
    }

    [Fact]
    public void Dispose_releases_dock_workspace_panel_instances()
    {
        var disposable = new RecordingDisposable();
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable));
        var actions = new WorkbenchActionRegistry();
        var viewModel = new MainWindowViewModel(
            panels,
            actions,
            savedLayout: null);

        viewModel.Dispose();

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void Dispose_closes_floating_windows_before_releasing_main_workspace()
    {
        var disposalOrder = new List<string>();
        var disposable = new RecordingDisposable(() => disposalOrder.Add("main"));
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable));
        var actions = new WorkbenchActionRegistry();
        var viewModel = new MainWindowViewModel(
            panels,
            actions,
            savedLayout: null);
        viewModel.SetFloatingWindowCallbacks(
            () => [],
            () => disposalOrder.Add("floating"),
            _ => false,
            _ => false);

        viewModel.Dispose();

        Assert.Equal(["floating", "main"], disposalOrder);
    }

    [Fact]
    public void Dispose_finishes_main_cleanup_after_floating_close_failure()
    {
        var floatingFailure = new InvalidOperationException("floating close failure");
        var mainFailure = new InvalidOperationException("main dispose failure");
        var disposable = new RecordingDisposable(() => throw mainFailure);
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable));
        var viewModel = new MainWindowViewModel(
            panels,
            new WorkbenchActionRegistry(),
            savedLayout: null);
        viewModel.SetFloatingWindowCallbacks(
            () => [],
            () => throw floatingFailure,
            _ => false,
            _ => false);

        var exception = Assert.Throws<AggregateException>(viewModel.Dispose);

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(floatingFailure, item),
            item => Assert.Same(mainFailure, item));
        Assert.True(disposable.IsDisposed);
        viewModel.Dispose();
    }

    [Fact]
    public void Main_workspace_waits_for_actual_window_focus_before_activating_panel()
    {
        var content = new RecordingLifecycleSink();
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var viewModel = new MainWindowViewModel(
            panels,
            new WorkbenchActionRegistry(),
            savedLayout: null);

        Assert.False(viewModel.DockWorkspace.IsHostFocused);
        Assert.Equal(["attached"], content.Events);

        viewModel.DockWorkspace.SetHostFocusState(true);

        Assert.Equal(["attached", "activated"], content.Events);
    }

    [Fact]
    public void Restored_floating_window_borrows_panel_instances_until_main_session_disposes()
    {
        var disposable = new RecordingDisposable();
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            EditorDockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable));
        var actions = new WorkbenchActionRegistry();
        var snapshot = new EditorDockLayoutSnapshot
        {
            Version = 1,
            FloatingWindows =
            {
                new EditorDockFloatingWindowSnapshot
                {
                    X = 16,
                    Y = 24,
                    Width = 480,
                    Height = 320,
                    ActiveWindowId = "floating-panel",
                    Root = new EditorDockLayoutNodeSnapshot
                    {
                        Kind = "Window",
                        Id = "node-floating-panel",
                        WindowId = "floating-panel",
                        WindowTitle = "Panel",
                        WindowArea = EditorDockArea.Center,
                        WindowRole = "Panel",
                        TabIds = ["panel"],
                        ActiveTabId = "panel",
                    },
                },
            },
        };
        var viewModel = new MainWindowViewModel(
            panels,
            actions,
            snapshot);

        var request = Assert.Single(viewModel.ConsumeRestoredFloatingWindowRequests());
        request.Window.Dispose();

        Assert.False(disposable.IsDisposed);
        viewModel.Dispose();

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void PanelMenuItems_follow_registered_workbench_actions()
    {
        var viewModel = CreateMainWindowViewModel();

        Assert.Equal(
            ["scene-view", "hierarchy", "project", "inspector", "console", "problems", "frame-debugger", "ui-style"],
            viewModel.PanelMenuItems.Select(item => item.PanelId));
        Assert.Equal(
            ["Scene View", "Hierarchy", "Project", "Inspector", "Console", "Problems", "Frame Debugger", "UI Style"],
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
    public void Tools_menu_command_updates_latest_status_message()
    {
        var viewModel = CreateMainWindowViewModel();
        var item = Assert.Single(viewModel.ToolsMenuItems);

        item.OpenCommand.Execute(null);

        Assert.True(viewModel.HasStatusMessage);
        Assert.True(viewModel.IsStatusMessageSuccess);
        Assert.Equal(EditorStatusMessageSeverity.Success, viewModel.LastStatusMessage?.Severity);
        Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
        Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.StatusMessageText);
        Assert.False(viewModel.CanOpenStatusMessageTarget);
        Assert.False(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));
    }

    [Fact]
    public void Command_status_message_publishes_debug_diagnostic_and_updates_latest_status()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = CreateMainWindowViewModel(diagnostics: diagnostics);
        var item = Assert.Single(viewModel.ToolsMenuItems);

        item.OpenCommand.Execute(null);

        var record = Assert.Single(diagnostics.GetRecentDiagnostics());
        Assert.Equal(EditorDiagnosticChannel.Debug, record.Channel);
        Assert.Equal("workbench.commandPalette.open", record.Source);
        Assert.Equal("workbench", record.Category);
        Assert.Equal(record.Message, viewModel.StatusMessageText);
    }

    [Fact]
    public void External_diagnostic_updates_latest_status_message()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = CreateMainWindowViewModel(diagnostics: diagnostics);

        var record = diagnostics.Publish(
            EditorDiagnosticSeverity.Error,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Missing reference.");

        Assert.True(viewModel.HasStatusMessage);
        Assert.True(viewModel.IsStatusMessageError);
        Assert.Null(viewModel.LastStatusMessage);
        Assert.Equal(record.Message, viewModel.StatusMessageText);
        Assert.Equal("1 error", viewModel.DiagnosticSummary);

        diagnostics.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Fallback material used.");

        Assert.Equal("1 error, 1 warning", viewModel.DiagnosticSummary);

        diagnostics.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Fallback texture used.");

        Assert.Equal("1 error, 2 warnings", viewModel.DiagnosticSummary);
    }

    [Fact]
    public void ActiveBackgroundTaskSummaryShowsRunningTask()
    {
        var tasks = new EditorBackgroundTaskService();
        tasks.Start("project.open", "Opening Project", canCancel: false);

        var viewModel = CreateMainWindowViewModel(backgroundTasks: tasks);

        Assert.True(viewModel.HasActiveBackgroundTasks);
        Assert.Equal("Opening Project", viewModel.ActiveBackgroundTaskTitle);
        Assert.Equal(string.Empty, viewModel.ActiveBackgroundTaskMessage);
    }

    [Fact]
    public void BackgroundTaskSummary_updates_when_task_starts_after_construction()
    {
        var tasks = new EditorBackgroundTaskService();
        var viewModel = CreateMainWindowViewModel(backgroundTasks: tasks);

        Assert.False(viewModel.HasActiveBackgroundTasks);

        tasks.Start("project.open", "Opening Project", canCancel: false);

        Assert.True(viewModel.HasActiveBackgroundTasks);
        Assert.Equal("Opening Project", viewModel.ActiveBackgroundTaskTitle);
    }

    [Fact]
    public void BackgroundTaskSummary_posts_update_when_task_changes_off_ui_thread()
    {
        var tasks = new EditorBackgroundTaskService();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = CreateMainWindowViewModel(backgroundTasks: tasks, uiDispatcher: dispatcher);

        tasks.Start("project.open", "Opening Project", canCancel: false);

        Assert.Equal(1, dispatcher.PostCount);
        Assert.False(viewModel.HasActiveBackgroundTasks);

        dispatcher.RunPostedActions();

        Assert.True(viewModel.HasActiveBackgroundTasks);
        Assert.Equal("Opening Project", viewModel.ActiveBackgroundTaskTitle);
    }

    [Fact]
    public void BackgroundTaskSummary_refreshes_immediately_when_task_changes_on_ui_thread()
    {
        var tasks = new EditorBackgroundTaskService();
        var dispatcher = new CapturingUiDispatcher(hasAccess: true);
        var viewModel = CreateMainWindowViewModel(backgroundTasks: tasks, uiDispatcher: dispatcher);

        tasks.Start("project.open", "Opening Project", canCancel: false);

        Assert.Equal(0, dispatcher.PostCount);
        Assert.True(viewModel.HasActiveBackgroundTasks);
        Assert.Equal("Opening Project", viewModel.ActiveBackgroundTaskTitle);
    }

    [Fact]
    public void Constructor_exposes_injected_lifecycle_event_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();

        var viewModel = CreateMainWindowViewModel(lifecycleEvents: lifecycleEvents);

        Assert.Same(lifecycleEvents, viewModel.LifecycleEvents);
        Assert.Same(lifecycleEvents, viewModel.DockWorkspace.LifecycleEvents);
    }

    [Fact]
    public void Restored_floating_window_requests_share_lifecycle_event_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();
        var snapshot = new EditorDockLayoutSnapshot
        {
            Version = 1,
            FloatingWindows =
            {
                new EditorDockFloatingWindowSnapshot
                {
                    X = 16,
                    Y = 24,
                    Width = 480,
                    Height = 320,
                    ActiveWindowId = "floating-inspector",
                    Root = new EditorDockLayoutNodeSnapshot
                    {
                        Kind = "Window",
                        Id = "node-floating-inspector",
                        WindowId = "floating-inspector",
                        WindowTitle = "Inspector",
                        WindowArea = EditorDockArea.Right,
                        WindowRole = "Selection context",
                        TabIds = ["inspector"],
                        ActiveTabId = "inspector",
                    },
                },
            },
        };
        var composition = CreateDefaultComposition();
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            snapshot,
            lifecycleEvents: lifecycleEvents);

        var request = Assert.Single(viewModel.ConsumeRestoredFloatingWindowRequests());

        Assert.Same(lifecycleEvents, request.Window.LifecycleEvents);
        Assert.Same(lifecycleEvents, request.Window.DockWorkspace.LifecycleEvents);
    }

    [Fact]
    public void BackgroundTaskSummary_clears_when_last_task_completes()
    {
        var tasks = new EditorBackgroundTaskService();
        var id = tasks.Start("project.open", "Opening Project", canCancel: false);
        var viewModel = CreateMainWindowViewModel(backgroundTasks: tasks);

        tasks.Complete(id, "Opened");

        Assert.False(viewModel.HasActiveBackgroundTasks);
        Assert.Equal(string.Empty, viewModel.ActiveBackgroundTaskTitle);
        Assert.Equal(string.Empty, viewModel.ActiveBackgroundTaskMessage);
    }

    [Fact]
    public void HelpMenuItems_follow_registered_workbench_actions()
    {
        var viewModel = CreateMainWindowViewModel();

        var item = Assert.Single(viewModel.HelpMenuItems);
        Assert.Equal("workbench.about.open", item.CommandId);
        Assert.Equal("About", item.Header);
        Assert.Equal("Help/About", item.MenuPath);
    }

    [Fact]
    public void HelpMenuItems_open_about_dialog_through_command_route()
    {
        var viewModel = CreateMainWindowViewModel();
        var item = Assert.Single(viewModel.HelpMenuItems);

        item.OpenCommand.Execute(null);

        Assert.True(viewModel.DialogHost.IsOpen);
        Assert.Equal("About Studio", viewModel.DialogHost.Title);
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
    public void CommandPalette_records_recent_command_after_main_window_route_success()
    {
        var viewModel = CreateMainWindowViewModel();
        var hierarchy = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
        Assert.True(viewModel.DockWorkspace.CloseTab(hierarchy));

        viewModel.CommandPalette.OpenCommand.Execute(null);
        viewModel.CommandPalette.Query = "hierarchy";
        viewModel.CommandPalette.ExecuteSelectedCommand.Execute(null);
        viewModel.CommandPalette.OpenCommand.Execute(null);

        Assert.Equal("Recent", viewModel.CommandPalette.FilteredItems[0].Title);
        Assert.Equal("Hierarchy", viewModel.CommandPalette.FilteredItems[1].Title);
        Assert.True(viewModel.DockWorkspace.ContainsPanel("hierarchy"));
    }

    [Theory]
    [InlineData("frame-debugger")]
    [InlineData("ui-style")]
    public void Panel_menu_command_reopens_optional_tools_excluded_from_default_layout(
        string panelId)
    {
        var composition = CreateDefaultComposition();
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            uiDispatcher: new CapturingUiDispatcher(hasAccess: true),
            defaultLayoutFactory: EditorWorkbenchLayoutPreset.CreateDefault);
        var menuItem = viewModel.PanelMenuItems.Single(item => item.PanelId == panelId);
        Assert.False(viewModel.DockWorkspace.ContainsPanel(panelId));

        menuItem.OpenCommand.Execute(null);

        Assert.True(viewModel.DockWorkspace.ContainsPanel(panelId));
    }

    [Fact]
    public void Command_palette_failure_updates_local_and_global_status_message()
    {
        var actions = new WorkbenchActionRegistry();
        actions.Register(new WorkbenchActionDescriptor(
            "workbench.panel.missing",
            "Missing Panel",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Missing",
            TargetId: "missing-panel",
            Category: "Window"));
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            actions,
            savedLayout: null);

        viewModel.CommandPalette.OpenCommand.Execute(null);
        viewModel.CommandPalette.Query = "missing";
        viewModel.CommandPalette.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.CommandPalette.HasLastResultMessage);
        Assert.True(viewModel.HasStatusMessage);
        Assert.True(viewModel.IsStatusMessageError);
        Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
        Assert.Equal(viewModel.CommandPalette.LastResultMessage, viewModel.StatusMessageText);
        Assert.False(viewModel.CanOpenStatusMessageTarget);
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
    public void Shortcut_command_updates_latest_status_message()
    {
        var viewModel = CreateMainWindowViewModel();

        var result = viewModel.ExecuteShortcut(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: false);

        Assert.NotNull(result);
        Assert.True(viewModel.HasStatusMessage);
        Assert.True(viewModel.IsStatusMessageSuccess);
        Assert.Equal(EditorStatusMessageSource.Command, viewModel.LastStatusMessage?.Source);
        Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.StatusMessageText);
        Assert.Null(viewModel.LastStatusMessage?.TargetPanelId);
    }

    [Fact]
    public void Status_message_raises_visibility_message_severity_and_target_notifications()
    {
        var changedProperties = new List<string>();
        var viewModel = CreateMainWindowViewModel();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);

        viewModel.PublishStatusMessage(new EditorStatusMessageSnapshot(
            EditorStatusMessageSeverity.Debug,
            EditorStatusMessageSource.Console,
            "Console debug line",
            TargetPanelId: "console"));

        Assert.Contains(nameof(MainWindowViewModel.LastStatusMessage), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.HasStatusMessage), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.StatusMessageText), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageDebug), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageInfo), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageSuccess), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageWarning), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsStatusMessageError), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.CanOpenStatusMessageTarget), changedProperties);
    }

    [Fact]
    public void Passive_command_status_message_does_not_open_target_panel()
    {
        var viewModel = CreateMainWindowViewModel();
        var console = viewModel.DockWorkspace.BottomWindow.Tabs.Single(tab => tab.Id == "console");
        Assert.True(viewModel.DockWorkspace.CloseTab(console));

        viewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

        Assert.False(viewModel.CanOpenStatusMessageTarget);
        Assert.False(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));
        viewModel.OpenStatusMessageTargetCommand.Execute(null);
        Assert.False(viewModel.DockWorkspace.ContainsPanel("console"));
    }

    [Fact]
    public void Console_targeted_status_message_opens_console_panel()
    {
        var viewModel = CreateMainWindowViewModel();
        var console = viewModel.DockWorkspace.BottomWindow.Tabs.Single(tab => tab.Id == "console");
        Assert.True(viewModel.DockWorkspace.CloseTab(console));
        viewModel.PublishStatusMessage(new EditorStatusMessageSnapshot(
            EditorStatusMessageSeverity.Debug,
            EditorStatusMessageSource.Console,
            "Console debug line",
            TargetPanelId: "console"));

        Assert.True(viewModel.CanOpenStatusMessageTarget);
        Assert.True(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));

        viewModel.OpenStatusMessageTargetCommand.Execute(null);

        Assert.True(viewModel.DockWorkspace.ContainsPanel("console"));
    }

    [Fact]
    public void Unknown_targeted_status_message_does_not_enable_or_throw()
    {
        var viewModel = CreateMainWindowViewModel();
        viewModel.PublishStatusMessage(new EditorStatusMessageSnapshot(
            EditorStatusMessageSeverity.Error,
            EditorStatusMessageSource.Console,
            "Unknown target",
            TargetPanelId: "missing-panel"));

        Assert.False(viewModel.CanOpenStatusMessageTarget);
        Assert.False(viewModel.OpenStatusMessageTargetCommand.CanExecute(null));
        viewModel.OpenStatusMessageTargetCommand.Execute(null);
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
                WindowArea = EditorDockArea.Right,
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

    private static MainWindowViewModel CreateMainWindowViewModel(
        IEditorBackgroundTaskService? backgroundTasks = null,
        IEditorUiDispatcher? uiDispatcher = null,
        IEditorLifecycleEventService? lifecycleEvents = null,
        IEditorDiagnosticService? diagnostics = null,
        IProjectOpenSessionSnapshotSource? projectOpenSessions = null,
        IProjectSessionService? projectSessions = null)
    {
        uiDispatcher ??= new CapturingUiDispatcher(hasAccess: true);
        diagnostics ??= new EditorDiagnosticService();
        projectOpenSessions ??= new ProjectOpenSessionSnapshotSource();
        var composition = CreateDefaultComposition(
            diagnostics: diagnostics);

        return new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            backgroundTasks: backgroundTasks,
            uiDispatcher: uiDispatcher,
            lifecycleEvents: lifecycleEvents,
            diagnostics: diagnostics,
            projectOpenSessions: projectOpenSessions,
            projectSessions: projectSessions);
    }

    private static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        return StudioCompositionRoot.CreateDefaultComposition(
            selectionService,
            diagnostics);
    }

    private static ProjectOpenSessionSnapshot CreateReadyProjectOpenSnapshot() =>
        new(
            ProjectOpenSessionState.Ready,
            ProjectOpenNextAction.ActivateProjectProfile,
            new ProjectOpenSummarySnapshot(
                "Example",
                Guid.Parse("7b535774-005d-47ff-90d7-83165df8bac8"),
                assetSourceRootCount: 1));

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        private readonly List<Action> postedActions_ = [];

        public int PostCount => postedActions_.Count;

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            postedActions_.Add(action);
        }

        public void RunPostedActions()
        {
            foreach (var action in postedActions_.ToArray())
            {
                action();
            }

            postedActions_.Clear();
        }
    }

    private sealed class StubProjectSessionService : IProjectSessionService
    {
        public event EventHandler? SnapshotChanged;

        public ProjectSessionSnapshot Current { get; private set; } =
            ProjectSessionSnapshot.NoProject;

        public string? CreatedRoot { get; private set; }

        public string? CreatedName { get; private set; }

        public string? OpenedRoot { get; private set; }

        public ProjectSessionOperationResult CreateMinimalProject(
            string projectRoot,
            string projectName)
        {
            CreatedRoot = projectRoot;
            CreatedName = projectName;
            return Activate(projectRoot, projectName);
        }

        public ProjectSessionOperationResult OpenProject(string projectRoot)
        {
            OpenedRoot = projectRoot;
            var name = Path.GetFileName(projectRoot);
            return Activate(projectRoot, name);
        }

        public void Publish(ProjectSessionSnapshot snapshot)
        {
            Current = snapshot;
            SnapshotChanged?.Invoke(this, EventArgs.Empty);
        }

        private ProjectSessionOperationResult Activate(
            string projectRoot,
            string projectName)
        {
            Publish(ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    projectRoot,
                    projectName,
                    Guid.Parse("45ad5a9c-4c1f-4723-966c-21c0ac638932"))));
            return ProjectSessionOperationResult.Success(
                Current,
                $"Opened project '{projectName}'.");
        }
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

    private sealed class RecordingDisposable(Action? onDispose = null) : IDisposable
    {
        public bool IsDisposed { get; private set; }

        public void Dispose()
        {
            IsDisposed = true;
            onDispose?.Invoke();
        }
    }

    private sealed class RecordingLifecycleSink : IEditorPanelLifecycleSink
    {
        public List<string> Events { get; } = [];

        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            Events.Add("attached");
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            Events.Add("activated");
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            Events.Add("deactivated");
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            Events.Add("detached");
        }
    }
}

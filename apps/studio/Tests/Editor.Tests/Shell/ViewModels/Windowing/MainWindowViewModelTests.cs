using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.Workbench;
using Editor.Core.Services;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Selection;
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
    public void Dispose_releases_dock_workspace_panel_instances()
    {
        var disposable = new RecordingDisposable();
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            DockArea.Center,
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
            DockArea.Center,
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
    public void Restored_floating_window_view_model_dispose_releases_panel_instances()
    {
        var disposable = new RecordingDisposable();
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            DockArea.Center,
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
                        WindowArea = DockArea.Center,
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

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void PanelMenuItems_follow_registered_workbench_actions()
    {
        var viewModel = CreateMainWindowViewModel();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems", "frame-debugger", "ui-style"],
            viewModel.PanelMenuItems.Select(item => item.PanelId));
        Assert.Equal(
            ["Scene View", "Hierarchy", "Inspector", "Console", "Problems", "Frame Debugger", "UI Style"],
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
    public void Tools_menu_command_updates_latest_command_feedback()
    {
        var viewModel = CreateMainWindowViewModel();
        var item = Assert.Single(viewModel.ToolsMenuItems);

        item.OpenCommand.Execute(null);

        Assert.True(viewModel.HasCommandFeedback);
        Assert.True(viewModel.IsCommandFeedbackSuccess);
        Assert.Equal(EditorCommandFeedbackSeverity.Success, viewModel.LastCommandFeedback?.Severity);
        Assert.Equal("workbench.commandPalette.open", viewModel.LastCommandFeedback?.CommandId);
        Assert.Equal("Command 'workbench.commandPalette.open' completed.", viewModel.CommandFeedbackMessage);
    }

    [Fact]
    public void Command_feedback_publishes_debug_diagnostic_and_updates_latest_status()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = CreateMainWindowViewModel(diagnostics: diagnostics);
        var item = Assert.Single(viewModel.ToolsMenuItems);

        item.OpenCommand.Execute(null);

        var record = Assert.Single(diagnostics.GetRecentDiagnostics());
        Assert.Equal(EditorDiagnosticChannel.Debug, record.Channel);
        Assert.Equal("workbench.commandPalette.open", record.Source);
        Assert.Equal("workbench", record.Category);
        Assert.Equal(record.Message, viewModel.CommandFeedbackMessage);
    }

    [Fact]
    public void External_diagnostic_updates_latest_status_feedback()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = CreateMainWindowViewModel(diagnostics: diagnostics);

        var record = diagnostics.Publish(
            EditorDiagnosticSeverity.Error,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Missing reference.");

        Assert.True(viewModel.HasCommandFeedback);
        Assert.True(viewModel.IsCommandFeedbackError);
        Assert.Null(viewModel.LastCommandFeedback);
        Assert.Equal(record.Message, viewModel.CommandFeedbackMessage);
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
                        WindowArea = DockArea.Right,
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

    [Fact]
    public void Command_palette_failure_updates_local_and_global_feedback()
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
        Assert.True(viewModel.HasCommandFeedback);
        Assert.True(viewModel.IsCommandFeedbackError);
        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, viewModel.LastCommandFeedback?.Status);
        Assert.Equal(viewModel.CommandPalette.LastResultMessage, viewModel.CommandFeedbackMessage);
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
    public void Shortcut_command_updates_latest_command_feedback()
    {
        var viewModel = CreateMainWindowViewModel();

        var result = viewModel.ExecuteShortcut(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: false);

        Assert.NotNull(result);
        Assert.True(viewModel.HasCommandFeedback);
        Assert.True(viewModel.IsCommandFeedbackSuccess);
        Assert.Equal("workbench.commandPalette.open", viewModel.LastCommandFeedback?.CommandId);
    }

    [Fact]
    public void Command_feedback_raises_visibility_message_and_severity_notifications()
    {
        var changedProperties = new List<string>();
        var viewModel = CreateMainWindowViewModel();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);

        viewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

        Assert.Contains(nameof(MainWindowViewModel.LastCommandFeedback), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.HasCommandFeedback), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.CommandFeedbackMessage), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackSuccess), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackWarning), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackError), changedProperties);
        Assert.Contains(nameof(MainWindowViewModel.IsCommandFeedbackInfo), changedProperties);
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
                WindowArea = DockArea.Right,
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
        IEditorDiagnosticService? diagnostics = null)
    {
        uiDispatcher ??= new CapturingUiDispatcher(hasAccess: true);
        diagnostics ??= new EditorDiagnosticService();
        var composition = CreateDefaultComposition(diagnostics: diagnostics);

        return new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout: null,
            backgroundTasks: backgroundTasks,
            uiDispatcher: uiDispatcher,
            lifecycleEvents: lifecycleEvents,
            diagnostics: diagnostics);
    }

    private static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        return StudioCompositionRoot.CreateDefaultComposition(selectionService, diagnostics);
    }

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
}

using System.Linq;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.TestSupport;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Commands;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Panels;
using Editor;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Docking;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioShellHeadlessTests
{
    [AvaloniaFact]
    public void Production_shell_realizes_starting_and_empty_states_with_stable_semantics()
    {
        Assert.IsType<App>(Avalonia.Application.Current);
        using var viewModel = StudioShellTestFactory.Create();
        var window = new MainWindow
        {
            DataContext = viewModel,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var starting = Assert.IsType<Border>(window.FindControl<Border>("StartingState"));
            var startingText = Assert.IsType<TextBlock>(
                window.FindControl<TextBlock>("StartingStateText"));
            Assert.True(starting.IsVisible);
            Assert.Equal("Starting", startingText.Text);
            Assert.Equal(
                "StudioShellStartingState",
                AutomationProperties.GetAutomationId(starting));
            Assert.Equal("Studio startup state", AutomationProperties.GetName(starting));
            Assert.Equal(
                AutomationControlType.StatusBar,
                AutomationProperties.GetControlTypeOverride(starting));

            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();

            var emptyWorkspace = Assert.IsType<Grid>(
                window.FindControl<Grid>("WorkspaceState"));
            var noProject = Assert.IsType<Border>(
                window.FindControl<Border>("NoProjectState"));
            var noDocument = Assert.IsType<Border>(
                window.FindControl<Border>("NoDocumentState"));
            Assert.False(starting.IsVisible);
            Assert.True(emptyWorkspace.IsVisible);
            Assert.Equal(
                "No Project",
                window.FindControl<TextBlock>("NoProjectStateText")?.Text);
            Assert.Equal(
                "No Document",
                window.FindControl<TextBlock>("NoDocumentStateText")?.Text);
            Assert.Equal(
                "StudioShellNoProjectState",
                AutomationProperties.GetAutomationId(noProject));
            Assert.Equal(
                "StudioShellNoDocumentState",
                AutomationProperties.GetAutomationId(noDocument));
            Assert.Equal(
                AutomationControlType.Group,
                AutomationProperties.GetControlTypeOverride(noProject));
            Assert.Equal(
                AutomationControlType.Group,
                AutomationProperties.GetControlTypeOverride(noDocument));
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async System.Threading.Tasks.Task Create_button_projects_the_authoritative_project_session()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out var dialogs);
        var window = new MainWindow
        {
            DataContext = viewModel,
        };
        dialogs.ParentDirectory = "C:\\Projects";
        viewModel.NewProjectName = "Sample";
        var ready = ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                ProjectSessionId.CreateNew(),
                System.Guid.NewGuid(),
                "Sample",
                "C:\\Projects\\Sample"),
            new SceneDocumentSnapshot(
                System.Guid.NewGuid(),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.CreateHandler = (_, _, _, _) =>
        {
            projectSession.Publish(ready);
            return System.Threading.Tasks.ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    ready,
                    "Created project 'Sample'."));
        };

        try
        {
            window.Show();
            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();
            var create = Assert.IsType<Button>(
                window.FindControl<Button>("CreateProjectButton"));

            create.Command!.Execute(create.CommandParameter);
            using var timeout = new System.Threading.CancellationTokenSource(
                System.TimeSpan.FromSeconds(2));
            while (viewModel.IsProjectOperationRunning)
            {
                Dispatcher.UIThread.RunJobs();
                await System.Threading.Tasks.Task.Delay(10, timeout.Token);
            }
            Dispatcher.UIThread.RunJobs();

            Assert.False(window.FindControl<Border>("NoProjectState")!.IsVisible);
            Assert.True(window.FindControl<Grid>("ActiveProjectState")!.IsVisible);
            Assert.Equal("Sample", viewModel.ProjectStateText);
            Assert.Equal("C:\\Projects\\Sample", viewModel.ProjectPathText);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async System.Threading.Tasks.Task Create_mesh_button_selects_the_authoritative_receipt_entity()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var window = new MainWindow
        {
            DataContext = viewModel,
        };
        var project = new ActiveProjectSnapshot(
            ProjectSessionId.CreateNew(),
            System.Guid.NewGuid(),
            "Sample",
            "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            project,
            new SceneDocumentSnapshot(
                System.Guid.NewGuid(),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.Publish(initial);
        var objectId = System.Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var updated = ProjectSessionSnapshot.Ready(
            project,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.CreateMeshEntityHandler = (_, mesh, _) =>
        {
            Assert.Equal(SceneMeshReference.DirectionalWedgeValidation, mesh);
            projectSession.Publish(updated);
            return System.Threading.Tasks.ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    updated,
                    "Created a mesh scene entity.",
                    objectId));
        };

        try
        {
            window.Show();
            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();
            var createMesh = Assert.IsType<Button>(
                window.FindControl<Button>("CreateMeshEntityButton"));

            createMesh.Command!.Execute(createMesh.CommandParameter);
            using var timeout = new System.Threading.CancellationTokenSource(
                System.TimeSpan.FromSeconds(2));
            while (viewModel.IsProjectOperationRunning)
            {
                Dispatcher.UIThread.RunJobs();
                await System.Threading.Tasks.Task.Delay(10, timeout.Token);
            }
            Dispatcher.UIThread.RunJobs();

            Assert.Same(entity, viewModel.SelectedEntity);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Main_menu_projects_registered_actions_and_reopens_all_panels()
    {
        using var viewModel = StudioShellTestFactory.Create();
        var window = new MainWindow { DataContext = viewModel };

        try
        {
            window.Show();
            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();
            var menu = Assert.IsType<Menu>(window.FindControl<Menu>("StudioMainMenu"));
            var topLevel = menu.Items.OfType<MenuItem>().ToArray();
            Assert.Equal(["File", "Edit", "Scene", "Window"],
                topLevel.Select(item => item.Header?.ToString() ?? string.Empty).ToArray());

            var windowMenu = topLevel.Single(item =>
                string.Equals(item.Header?.ToString(), "Window",
                    System.StringComparison.Ordinal));
            var panelsMenu = Assert.Single(windowMenu.Items.OfType<MenuItem>());
            Assert.Equal("Panels", panelsMenu.Header?.ToString());
            var panelItems = panelsMenu.Items.OfType<MenuItem>().ToArray();
            Assert.Equal(4, panelItems.Length);
            foreach (var panelId in new[] { "hierarchy", "project", "scene-view", "inspector" })
            {
                Assert.True(viewModel.DockWorkspace.ClosePanel(panelId));
                Assert.False(viewModel.DockWorkspace.ContainsPanel(panelId));
                var expectedAction = panelId switch
                {
                    "hierarchy" => "studio.window.open-hierarchy-panel",
                    "project" => "studio.window.open-project-panel",
                    "scene-view" => "studio.window.open-scene-view-panel",
                    "inspector" => "studio.window.open-inspector-panel",
                    _ => throw new System.ArgumentOutOfRangeException(nameof(panelId)),
                };
                var item = panelItems.Single(candidate =>
                    string.Equals(candidate.Tag?.ToString(), expectedAction,
                        System.StringComparison.Ordinal));
                Assert.True(item.Command!.CanExecute(item.CommandParameter));
                item.Command.Execute(item.CommandParameter);
                Dispatcher.UIThread.RunJobs();
                Assert.True(viewModel.DockWorkspace.ContainsPanel(panelId));
            }
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Floating_window_action_identity_is_lifetime_stable_and_unique()
    {
        var first = new EditorDockFloatingWindow();
        var second = new EditorDockFloatingWindow();

        Assert.True(first.ActionTopLevelId.IsValid);
        Assert.Equal(first.ActionTopLevelId, first.ActionTopLevelId);
        Assert.NotEqual(first.ActionTopLevelId, second.ActionTopLevelId);
    }

    [AvaloniaFact]
    public void Floating_panel_button_projects_its_actual_top_level_and_panel()
    {
        using var shell = StudioShellTestFactory.Create();
        var snapshot = new EditorDockFloatingWindowSnapshot
        {
            ActiveWindowId = "floating-inspector-window",
            Root = new EditorDockLayoutNodeSnapshot
            {
                Kind = "Window",
                Id = "floating-inspector-node",
                WindowId = "floating-inspector-window",
                WindowTitle = "Inspector",
                WindowArea = EditorDockArea.Right,
                WindowRole = "Selection context",
                TabIds = ["inspector"],
                ActiveTabId = "inspector",
            },
        };
        Assert.True(shell.DockWorkspace.TryCreateFloatingWorkspace(
            snapshot,
            out var floatingWorkspace));
        var floatingViewModel = new EditorDockFloatingWindowViewModel(floatingWorkspace);
        var floatingWindow = new EditorDockFloatingWindow { DataContext = floatingViewModel };
        var inspector = Assert.IsType<StudioInspectorPanelViewModel>(
            floatingWorkspace.ActiveWindow!.ActiveTab!.Content);
        var button = new Button { DataContext = inspector };
        var host = new EditorDockPanelContentHost
        {
            Panel = floatingWorkspace.ActiveWindow.ActiveTab,
            Content = button,
        };
        floatingWindow.Content = host;

        try
        {
            floatingWindow.Show();
            Dispatcher.UIThread.RunJobs();

            Assert.True(StudioActionButton.TryResolvePresentation(
                button,
                shell,
                out var topLevelId,
                out var focusedPanelId));
            Assert.Equal(floatingWindow.ActionTopLevelId, topLevelId);
            Assert.Equal(new StudioPresentationId("inspector"), focusedPanelId);
            Assert.NotEqual(
                StudioShellViewModel.ActivePanelId(shell.DockWorkspace),
                focusedPanelId);
        }
        finally
        {
            floatingWindow.Close();
        }
    }

    [AvaloniaFact]
    public async System.Threading.Tasks.Task Floating_window_uses_the_same_shortcut_registry()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var mainWindow = new MainWindow { DataContext = viewModel };
        var floatingWindow = new EditorDockFloatingWindow();
        var nestedFloatingWindow = new EditorDockFloatingWindow();
        var project = new ActiveProjectSnapshot(
            ProjectSessionId.CreateNew(),
            System.Guid.NewGuid(),
            "Sample",
            "C:\\Projects\\Sample");
        var document = new SceneDocumentSnapshot(
            System.Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 2,
            savedRevision: 1,
            entities: []);
        var canUndo = ProjectSessionSnapshot.Ready(
            project,
            document,
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Transform",
            redoLabel: null);
        projectSession.Publish(canUndo);
        var undoCount = 0;
        projectSession.UndoHandler = _ =>
        {
            undoCount++;
            return System.Threading.Tasks.ValueTask.FromResult(
                ProjectSessionOperationResult.Success(canUndo, "Undid Transform."));
        };

        try
        {
            mainWindow.Show();
            viewModel.MarkReady();
            floatingWindow.Show(mainWindow);
            nestedFloatingWindow.Show(floatingWindow);
            Dispatcher.UIThread.RunJobs();

            Press(floatingWindow, Key.Z, RawInputModifiers.Control);
            await WaitForOperationAsync(viewModel);
            Press(nestedFloatingWindow, Key.Z, RawInputModifiers.Control);
            await WaitForOperationAsync(viewModel);

            Assert.Equal(2, undoCount);
        }
        finally
        {
            nestedFloatingWindow.Close();
            floatingWindow.Close();
            mainWindow.Close();
        }
    }

    [AvaloniaFact]
    public async System.Threading.Tasks.Task Document_shortcuts_route_after_focus_and_preserve_text_draft_undo()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var window = new MainWindow
        {
            DataContext = viewModel,
        };
        var project = new ActiveProjectSnapshot(
            ProjectSessionId.CreateNew(),
            System.Guid.NewGuid(),
            "Sample",
            "C:\\Projects\\Sample");
        var document = new SceneDocumentSnapshot(
            System.Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 2,
            savedRevision: 1,
            entities: []);
        var canUndo = ProjectSessionSnapshot.Ready(
            project,
            document,
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Transform Selected",
            redoLabel: null);
        projectSession.Publish(canUndo);
        ulong nextRevision = 2;
        ProjectSessionSnapshot HistorySnapshot(bool hasUndo)
        {
            nextRevision++;
            return ProjectSessionSnapshot.Ready(
                project,
                new SceneDocumentSnapshot(
                    document.SceneId,
                    document.Path,
                    nextRevision,
                    savedRevision: 1,
                    entities: []),
                hasUndo ? new ContentStateId(2) : new ContentStateId(1),
                new ContentStateId(1),
                canUndo: hasUndo,
                canRedo: !hasUndo,
                undoLabel: hasUndo ? "Transform Selected" : null,
                redoLabel: hasUndo ? null : "Transform Selected");
        }
        var undoCount = 0;
        var redoCount = 0;
        projectSession.UndoHandler = _ =>
        {
            undoCount++;
            return System.Threading.Tasks.ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    HistorySnapshot(hasUndo: false),
                    "Undid Transform Selected."));
        };
        projectSession.RedoHandler = _ =>
        {
            redoCount++;
            return System.Threading.Tasks.ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    HistorySnapshot(hasUndo: true),
                    "Redid Transform Selected."));
        };

        try
        {
            window.Show();
            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();
            var workspace = window.Content;
            var undoButton = Assert.IsType<Button>(
                window.FindControl<Button>("UndoSceneButton"));
            var redoButton = Assert.IsType<Button>(
                window.FindControl<Button>("RedoSceneButton"));
            Assert.True(undoButton.Command!.CanExecute(undoButton.CommandParameter));
            Assert.False(redoButton.Command!.CanExecute(redoButton.CommandParameter));
            Assert.Equal("Undo Transform Selected", undoButton.Content);
            Assert.Equal("Redo", redoButton.Content);
            Assert.Equal(
                "Undo Transform Selected",
                FindMenuItem(window, "Edit", "studio.edit.undo-scene").Header);

            var textBox = new TextBox
            {
                Text = "draft",
            };
            window.Content = textBox;
            textBox.Focus();
            Dispatcher.UIThread.RunJobs();
            Assert.Same(textBox, window.FocusManager?.GetFocusedElement());

            Press(window, Key.Z, RawInputModifiers.Control);
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(0, undoCount);
            Assert.Null(textBox.Text);

            textBox.IsUndoEnabled = false;
            textBox.Text = "guarded draft";
            Press(window, Key.Z, RawInputModifiers.Control);
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(0, undoCount);
            Assert.Equal("guarded draft", textBox.Text);

            window.Content = workspace;
            Dispatcher.UIThread.RunJobs();
            Assert.True(undoButton.Focus());
            Press(window, Key.Z, RawInputModifiers.Control);
            await WaitForOperationAsync(viewModel);

            Assert.Equal(1, undoCount);
            Assert.False(undoButton.Command!.CanExecute(undoButton.CommandParameter));
            Assert.True(redoButton.Command!.CanExecute(redoButton.CommandParameter));
            Assert.Equal("Undo", undoButton.Content);
            Assert.Equal("Redo Transform Selected", redoButton.Content);
            Assert.Equal(
                "Redo Transform Selected",
                FindMenuItem(window, "Edit", "studio.edit.redo-scene").Header);

            Press(window, Key.Y, RawInputModifiers.Control);
            await WaitForOperationAsync(viewModel);
            Assert.Equal(1, redoCount);

            Press(window, Key.Z, RawInputModifiers.Control);
            await WaitForOperationAsync(viewModel);
            Press(
                window,
                Key.Z,
                RawInputModifiers.Control | RawInputModifiers.Shift);
            await WaitForOperationAsync(viewModel);
            Assert.Equal(2, undoCount);
            Assert.Equal(2, redoCount);

            Press(
                window,
                Key.Z,
                RawInputModifiers.Control | RawInputModifiers.Alt);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(2, undoCount);

            Press(window, Key.Z, RawInputModifiers.Meta);
            await WaitForOperationAsync(viewModel);
            Assert.Equal(3, undoCount);

            Press(
                window,
                Key.Z,
                RawInputModifiers.Control | RawInputModifiers.Meta);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(3, undoCount);
        }
        finally
        {
            window.Close();
        }
    }

    private static async System.Threading.Tasks.Task WaitForOperationAsync(
        StudioShellViewModel viewModel)
    {
        using var timeout = new System.Threading.CancellationTokenSource(
            System.TimeSpan.FromSeconds(2));
        await System.Threading.Tasks.Task.Yield();
        do
        {
            Dispatcher.UIThread.RunJobs();
            await System.Threading.Tasks.Task.Delay(10, timeout.Token);
        }
        while (viewModel.IsProjectOperationRunning);
        Dispatcher.UIThread.RunJobs();
    }

    private static MenuItem FindMenuItem(
        MainWindow window,
        string topLevelHeader,
        string actionId)
    {
        var menu = Assert.IsType<Menu>(window.FindControl<Menu>("StudioMainMenu"));
        var topLevel = menu.Items.OfType<MenuItem>().Single(item =>
            string.Equals(item.Header?.ToString(), topLevelHeader,
                System.StringComparison.Ordinal));
        return topLevel.Items.OfType<MenuItem>().Single(item =>
            string.Equals(item.Tag?.ToString(), actionId,
                System.StringComparison.Ordinal));
    }

    private static void Press(
        TopLevel topLevel,
        Key key,
        RawInputModifiers modifiers) =>
        topLevel.KeyPress(
            key,
            modifiers,
            key == Key.Y ? PhysicalKey.Y : PhysicalKey.Z,
            key == Key.Y ? "y" : "z");
}

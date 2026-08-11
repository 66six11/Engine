using Avalonia;
using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.TestSupport;
using Editor;
using Editor.Shell.ViewModels.Windowing;
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
                entities: []));
        projectSession.CreateHandler = (_, _, _) =>
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
                entities: []));
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
                entities: [entity]));
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
}

using System;
using System.Collections.Generic;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Editor.Core.Abstractions;
using Editor.Features.Project.ViewModels;
using Xunit;

namespace Editor.Tests.Features.Project;

public sealed class ProjectPanelViewModelTests
{
    [Fact]
    public void No_project_state_is_explicit_and_actions_are_unavailable()
    {
        var viewModel = new ProjectPanelViewModel();

        Assert.Equal("No project", viewModel.ProjectDisplayName);
        Assert.Equal("No project", viewModel.StateLabel);
        Assert.Equal("No project is open", viewModel.StateTitle);
        Assert.Equal("Select Project", viewModel.PrimaryActionLabel);
        Assert.False(viewModel.CanSearch);
        Assert.False(viewModel.CanExecutePrimaryAction);
        Assert.False(string.IsNullOrWhiteSpace(viewModel.UnavailableReason));
        Assert.False(viewModel.HasDiagnostics);
    }

    [Theory]
    [MemberData(nameof(NonReadyStateCases))]
    public void Non_ready_states_project_canonical_state_and_next_action(
        ProjectOpenSessionState state,
        ProjectOpenNextAction nextAction,
        string expectedStateLabel,
        string expectedActionLabel)
    {
        var source = new ProjectOpenSessionSnapshotSource(
            new ProjectOpenSessionSnapshot(
                state,
                nextAction,
                project: null));
        using var viewModel = new ProjectPanelViewModel(source);

        Assert.Equal("No project", viewModel.ProjectDisplayName);
        Assert.Equal(expectedStateLabel, viewModel.StateLabel);
        Assert.Equal(expectedActionLabel, viewModel.PrimaryActionLabel);
        Assert.False(viewModel.CanExecutePrimaryAction);
    }

    [Fact]
    public void Ready_state_projects_project_name_without_enabling_unconnected_services()
    {
        var source = new ProjectOpenSessionSnapshotSource(CreateReadySnapshot());
        using var viewModel = new ProjectPanelViewModel(source);

        Assert.Equal("Example", viewModel.ProjectDisplayName);
        Assert.Equal("Bootstrap ready", viewModel.StateLabel);
        Assert.Equal("Activate Project Profile", viewModel.PrimaryActionLabel);
        Assert.False(viewModel.CanSearch);
        Assert.False(viewModel.CanExecutePrimaryAction);
    }

    [Fact]
    public void Primary_diagnostic_is_projected_inline()
    {
        var source = new ProjectOpenSessionSnapshotSource(
            new ProjectOpenSessionSnapshot(
                ProjectOpenSessionState.PendingBuild,
                ProjectOpenNextAction.BuildProjectHost,
                project: null,
                [
                    new ProjectOpenSessionDiagnosticSnapshot(
                        "project.host.missing",
                        "project/asharia.project.json",
                        "/projectCode",
                        "A matching project host is not installed."),
                ]));
        using var viewModel = new ProjectPanelViewModel(source);

        Assert.True(viewModel.HasDiagnostics);
        Assert.Equal("1 project-open diagnostic", viewModel.DiagnosticCountText);
        Assert.Equal("project.host.missing", viewModel.PrimaryDiagnosticCode);
        Assert.Equal(
            "A matching project host is not installed.",
            viewModel.PrimaryDiagnosticMessage);
        Assert.Equal(
            "project/asharia.project.json/projectCode",
            viewModel.PrimaryDiagnosticLocation);
    }

    [Fact]
    public void Snapshot_changes_are_dispatched_and_dispose_unsubscribes()
    {
        var source = new ProjectOpenSessionSnapshotSource();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new ProjectPanelViewModel(source, dispatcher);

        source.Publish(CreateReadySnapshot());

        Assert.Equal("No project", viewModel.ProjectDisplayName);
        Assert.Equal(1, dispatcher.PostCount);
        dispatcher.RunPostedActions();
        Assert.Equal("Example", viewModel.ProjectDisplayName);

        viewModel.Dispose();
        source.Publish(ProjectOpenSessionSnapshot.NoProject);

        Assert.Equal(0, dispatcher.PostCount);
        Assert.Equal("Example", viewModel.ProjectDisplayName);
    }

    public static IEnumerable<object[]> NonReadyStateCases()
    {
        yield return
        [
            ProjectOpenSessionState.Opening,
            ProjectOpenNextAction.InspectProject,
            "Opening",
            "Inspect Project",
        ];
        yield return
        [
            ProjectOpenSessionState.PendingBuild,
            ProjectOpenNextAction.BuildProjectHost,
            "Build required",
            "Build Project Host",
        ];
        yield return
        [
            ProjectOpenSessionState.PendingRestart,
            ProjectOpenNextAction.RestartEditor,
            "Restart required",
            "Restart Editor",
        ];
        yield return
        [
            ProjectOpenSessionState.RepairRequired,
            ProjectOpenNextAction.RepairDistribution,
            "Repair required",
            "Repair Distribution",
        ];
        yield return
        [
            ProjectOpenSessionState.UpgradeRequired,
            ProjectOpenNextAction.UpgradeEngine,
            "Upgrade required",
            "Upgrade Engine",
        ];
        yield return
        [
            ProjectOpenSessionState.SafeMode,
            ProjectOpenNextAction.OpenSafeMode,
            "Safe mode",
            "Open Safe Mode",
        ];
        yield return
        [
            ProjectOpenSessionState.FatalDistributionError,
            ProjectOpenNextAction.RepairEditorImage,
            "Editor image error",
            "Repair Editor Image",
        ];
    }

    private static ProjectOpenSessionSnapshot CreateReadySnapshot() =>
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
}

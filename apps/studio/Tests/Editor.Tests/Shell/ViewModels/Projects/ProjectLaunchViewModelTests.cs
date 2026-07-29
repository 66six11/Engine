using System;
using System.Collections.Generic;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Editor.Core.Abstractions;
using Editor.Shell.ViewModels.Projects;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Projects;

public sealed class ProjectLaunchViewModelTests
{
    [Fact]
    public void No_project_state_is_explicit_and_non_interactive()
    {
        var source = new ProjectOpenSessionSnapshotSource();
        using var viewModel = new ProjectLaunchViewModel(source);

        Assert.Equal("No project", viewModel.ProjectCandidateDisplayName);
        Assert.Equal("No project", viewModel.StateLabel);
        Assert.Equal("No project is open", viewModel.StateTitle);
        Assert.Equal("Next: Select Project", viewModel.NextStepText);
        Assert.False(viewModel.HasDiagnostics);
    }

    [Theory]
    [MemberData(nameof(NonReadyStateCases))]
    public void Non_ready_states_project_canonical_state_and_next_step(
        ProjectOpenSessionState state,
        ProjectOpenNextAction nextAction,
        string expectedStateLabel,
        string expectedNextStep)
    {
        var source = new ProjectOpenSessionSnapshotSource(
            new ProjectOpenSessionSnapshot(
                state,
                nextAction,
                project: null));
        using var viewModel = new ProjectLaunchViewModel(source);

        Assert.Equal("No project", viewModel.ProjectCandidateDisplayName);
        Assert.Equal(expectedStateLabel, viewModel.StateLabel);
        Assert.Equal(expectedNextStep, viewModel.NextStepText);
    }

    [Fact]
    public void Ready_state_projects_candidate_without_claiming_active_project()
    {
        var source = new ProjectOpenSessionSnapshotSource(CreateReadySnapshot());
        using var viewModel = new ProjectLaunchViewModel(source);

        Assert.Equal("Example", viewModel.ProjectCandidateDisplayName);
        Assert.Equal("Ready to open", viewModel.StateLabel);
        Assert.Equal("Next: Open Project", viewModel.NextStepText);
    }

    [Fact]
    public void Primary_diagnostic_keeps_manifest_and_pointer_separate()
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
        using var viewModel = new ProjectLaunchViewModel(source);

        Assert.True(viewModel.HasDiagnostics);
        Assert.Equal("1 project-open diagnostic", viewModel.DiagnosticCountText);
        Assert.Equal("project.host.missing", viewModel.PrimaryDiagnosticCode);
        Assert.Equal(
            "A matching project host is not installed.",
            viewModel.PrimaryDiagnosticMessage);
        Assert.Equal(
            "project/asharia.project.json",
            viewModel.PrimaryDiagnosticManifestPath);
        Assert.Equal("/projectCode", viewModel.PrimaryDiagnosticPointer);
    }

    [Fact]
    public void Snapshot_changes_are_dispatched_and_dispose_unsubscribes()
    {
        var source = new ProjectOpenSessionSnapshotSource();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new ProjectLaunchViewModel(source, dispatcher);

        source.Publish(CreateReadySnapshot());

        Assert.Equal("No project", viewModel.ProjectCandidateDisplayName);
        Assert.Equal(1, dispatcher.PostCount);
        dispatcher.RunPostedActions();
        Assert.Equal("Example", viewModel.ProjectCandidateDisplayName);

        viewModel.Dispose();
        source.Publish(ProjectOpenSessionSnapshot.NoProject);

        Assert.Equal(0, dispatcher.PostCount);
        Assert.Equal("Example", viewModel.ProjectCandidateDisplayName);
    }

    public static IEnumerable<object[]> NonReadyStateCases()
    {
        yield return
        [
            ProjectOpenSessionState.Opening,
            ProjectOpenNextAction.InspectProject,
            "Opening",
            "Next: Check Project",
        ];
        yield return
        [
            ProjectOpenSessionState.PendingBuild,
            ProjectOpenNextAction.BuildProjectHost,
            "Build required",
            "Next: Build Project Code",
        ];
        yield return
        [
            ProjectOpenSessionState.PendingRestart,
            ProjectOpenNextAction.RestartEditor,
            "Restart required",
            "Next: Restart Studio",
        ];
        yield return
        [
            ProjectOpenSessionState.RepairRequired,
            ProjectOpenNextAction.RepairDistribution,
            "Repair required",
            "Next: Repair Engine Installation",
        ];
        yield return
        [
            ProjectOpenSessionState.UpgradeRequired,
            ProjectOpenNextAction.UpgradeEngine,
            "Upgrade required",
            "Next: Use Compatible Engine Version",
        ];
        yield return
        [
            ProjectOpenSessionState.SafeMode,
            ProjectOpenNextAction.OpenSafeMode,
            "Safe mode",
            "Next: Open in Safe Mode",
        ];
        yield return
        [
            ProjectOpenSessionState.FatalDistributionError,
            ProjectOpenNextAction.RepairEditorImage,
            "Studio installation error",
            "Next: Repair Studio Installation",
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

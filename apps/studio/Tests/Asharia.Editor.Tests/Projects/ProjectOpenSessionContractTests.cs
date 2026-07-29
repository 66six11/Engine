using System;
using System.Linq;
using Asharia.Editor.Projects;
using Xunit;

namespace Asharia.Editor.Tests.Projects;

public sealed class ProjectOpenSessionContractTests
{
    [Fact]
    public void Project_open_contract_is_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(ProjectOpenSessionState),
            typeof(ProjectOpenNextAction),
            typeof(ProjectOpenSessionDiagnosticSnapshot),
            typeof(ProjectOpenSummarySnapshot),
            typeof(ProjectOpenSessionSnapshot),
            typeof(IProjectOpenSessionSnapshotSource),
        };

        Assert.All(
            types,
            type =>
            {
                Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name);
                Assert.Equal("Asharia.Editor.Projects", type.Namespace);
            });
    }

    [Fact]
    public void State_and_action_values_are_stable()
    {
        Assert.Equal(
            Enumerable.Range(0, 9),
            Enum.GetValues<ProjectOpenSessionState>()
                .Select(value => Convert.ToInt32(value)));
        Assert.Equal(
            Enumerable.Range(0, 9),
            Enum.GetValues<ProjectOpenNextAction>()
                .Select(value => Convert.ToInt32(value)));
    }

    [Fact]
    public void Snapshot_copies_diagnostics_and_preserves_bootstrap_ready_semantics()
    {
        var diagnostics = new[]
        {
            new ProjectOpenSessionDiagnosticSnapshot(
                "bootstrap.test",
                "asharia.bootstrap-session.json",
                "/state",
                "Test diagnostic."),
        };
        var project = new ProjectOpenSummarySnapshot(
            "Example",
            Guid.Parse("6ad468bb-e099-46d4-a91b-911e86cf7188"),
            assetSourceRootCount: 1);
        var snapshot = new ProjectOpenSessionSnapshot(
            ProjectOpenSessionState.Ready,
            ProjectOpenNextAction.ActivateProjectProfile,
            project,
            diagnostics);

        diagnostics[0] = diagnostics[0] with { Message = "Changed." };

        Assert.True(snapshot.IsBootstrapReady);
        Assert.Equal(project, snapshot.Project);
        Assert.Equal("Test diagnostic.", Assert.Single(snapshot.Diagnostics).Message);
    }

    [Fact]
    public void Snapshot_rejects_state_action_and_project_invariant_mismatches()
    {
        var project = new ProjectOpenSummarySnapshot(
            "Example",
            Guid.Parse("6ad468bb-e099-46d4-a91b-911e86cf7188"),
            assetSourceRootCount: 1);

        Assert.Throws<ArgumentException>(() => new ProjectOpenSessionSnapshot(
            ProjectOpenSessionState.Opening,
            ProjectOpenNextAction.SelectProject,
            project: null));
        Assert.Throws<ArgumentException>(() => new ProjectOpenSessionSnapshot(
            ProjectOpenSessionState.Ready,
            ProjectOpenNextAction.ActivateProjectProfile,
            project: null));
        Assert.Throws<ArgumentException>(() => new ProjectOpenSessionSnapshot(
            ProjectOpenSessionState.SafeMode,
            ProjectOpenNextAction.OpenSafeMode,
            project));
    }

    [Fact]
    public void Project_summary_rejects_invalid_identity()
    {
        Assert.Throws<ArgumentException>(() =>
            new ProjectOpenSummarySnapshot(
                string.Empty,
                Guid.NewGuid(),
                assetSourceRootCount: 1));
        Assert.Throws<ArgumentException>(() =>
            new ProjectOpenSummarySnapshot(
                "Example",
                Guid.Empty,
                assetSourceRootCount: 1));
    }
}

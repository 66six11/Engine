using System;
using Asharia.Editor.Projects;
using Xunit;

namespace Asharia.Editor.Tests.Projects;

public sealed class ProjectSessionContractTests
{
    private static readonly ActiveProjectSnapshot Project = new(
        @"D:\Projects\Example",
        "Example",
        Guid.Parse("51e86383-8a06-4c41-9267-ab10b0b67eb9"));

    [Fact]
    public void No_project_snapshot_has_no_active_project()
    {
        Assert.Equal(
            ProjectSessionState.NoProject,
            ProjectSessionSnapshot.NoProject.State);
        Assert.False(ProjectSessionSnapshot.NoProject.IsReady);
        Assert.Null(ProjectSessionSnapshot.NoProject.Project);
    }

    [Fact]
    public void Ready_snapshot_requires_an_active_project()
    {
        var snapshot = ProjectSessionSnapshot.Ready(Project);

        Assert.True(snapshot.IsReady);
        Assert.Same(Project, snapshot.Project);
        Assert.Equal("Example", snapshot.Project!.ProjectName);
    }

    [Fact]
    public void State_and_project_presence_must_match()
    {
        _ = Assert.Throws<ArgumentException>(() =>
            new ProjectSessionSnapshot(
                ProjectSessionState.NoProject,
                Project));
        _ = Assert.Throws<ArgumentException>(() =>
            new ProjectSessionSnapshot(
                ProjectSessionState.Ready,
                project: null));
    }

    [Fact]
    public void Active_project_rejects_missing_identity()
    {
        _ = Assert.Throws<ArgumentException>(() =>
            new ActiveProjectSnapshot(
                string.Empty,
                "Example",
                Project.ProjectId));
        _ = Assert.Throws<ArgumentException>(() =>
            new ActiveProjectSnapshot(
                Project.RootPath,
                string.Empty,
                Project.ProjectId));
        _ = Assert.Throws<ArgumentException>(() =>
            new ActiveProjectSnapshot(
                Project.RootPath,
                Project.ProjectName,
                Guid.Empty));
    }

    [Fact]
    public void Operation_result_keeps_success_and_failure_distinct()
    {
        var session = ProjectSessionSnapshot.Ready(Project);
        var success = ProjectSessionOperationResult.Success(
            session,
            "Opened project.");
        var failure = ProjectSessionOperationResult.Failure(
            "Project could not be opened.");

        Assert.True(success.Succeeded);
        Assert.Same(session, success.Session);
        Assert.False(failure.Succeeded);
        Assert.Null(failure.Session);
    }

    [Fact]
    public void Successful_operation_requires_a_ready_session()
    {
        _ = Assert.Throws<ArgumentException>(() =>
            ProjectSessionOperationResult.Success(
                ProjectSessionSnapshot.NoProject,
                "Opened project."));
    }
}

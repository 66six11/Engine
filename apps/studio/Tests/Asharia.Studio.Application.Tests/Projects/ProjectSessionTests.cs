using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectSessionTests
{
    [Fact]
    public async Task Create_publishes_one_authoritative_ready_snapshot()
    {
        var projectId = Guid.NewGuid();
        var gateway = new ControlledGateway
        {
            CreateResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot("C:\\Projects\\Sample", "Sample", projectId)),
        };
        await using var session = new ProjectSession(gateway);
        var changed = 0;
        session.SnapshotChanged += (_, _) => changed++;

        var result = await session.CreateProjectAsync("C:\\Projects", "Sample");

        Assert.True(result.Succeeded);
        Assert.True(result.Current.IsReady);
        Assert.Equal(projectId, result.Current.Project!.ProjectId);
        Assert.Equal("Sample", result.Current.Project.ProjectName);
        Assert.Equal(result.Current, session.Current);
        Assert.True(result.Current.Project.SessionId.IsValid);
        Assert.Equal(1, changed);
        Assert.NotEqual(Guid.Empty, gateway.LastCreateProjectId);
    }

    [Fact]
    public async Task Failed_open_preserves_the_last_successful_project()
    {
        var first = new ProjectDescriptorSnapshot(
            "C:\\Projects\\First",
            "First",
            Guid.NewGuid());
        var gateway = new ControlledGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(first),
        };
        await using var session = new ProjectSession(gateway);
        var opened = await session.OpenProjectAsync(first.RootPath);
        gateway.OpenResult = ProjectDescriptorOperationResult.Failed(
            new ProjectDescriptorFailure(
                ProjectDescriptorFailureKind.InvalidProject,
                "The descriptor is invalid."));

        var failed = await session.OpenProjectAsync("C:\\Broken");

        Assert.False(failed.Succeeded);
        Assert.Equal(ProjectDescriptorFailureKind.InvalidProject, failed.FailureKind);
        Assert.Same(opened.Current, failed.Current);
        Assert.Same(opened.Current, session.Current);
    }

    [Fact]
    public async Task Dispose_cancels_an_in_flight_operation_and_rejects_late_work()
    {
        var entered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var gateway = new ControlledGateway
        {
            OpenHandler = async token =>
            {
                entered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, token);
                throw new InvalidOperationException("Unreachable.");
            },
        };
        var session = new ProjectSession(gateway);
        var operation = session.OpenProjectAsync("C:\\Projects\\Sample").AsTask();
        await entered.Task;

        await session.DisposeAsync();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await operation);
        await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
            await session.OpenProjectAsync("C:\\Projects\\Late"));
    }

    private sealed class ControlledGateway : IProjectDescriptorGateway
    {
        public ProjectDescriptorOperationResult? CreateResult { get; set; }

        public ProjectDescriptorOperationResult? OpenResult { get; set; }

        public Func<CancellationToken, Task<ProjectDescriptorOperationResult>>? OpenHandler
        {
            get;
            set;
        }

        public Guid LastCreateProjectId { get; private set; }

        public ValueTask<ProjectDescriptorOperationResult> CreateMinimalProjectAsync(
            string parentDirectory,
            string projectName,
            Guid projectId,
            CancellationToken cancellationToken = default)
        {
            LastCreateProjectId = projectId;
            return ValueTask.FromResult(
                CreateResult ?? throw new InvalidOperationException("Create result is missing."));
        }

        public ValueTask<ProjectDescriptorOperationResult> OpenProjectAsync(
            string projectPath,
            CancellationToken cancellationToken = default) =>
            OpenHandler is null
                ? ValueTask.FromResult(
                    OpenResult ?? throw new InvalidOperationException("Open result is missing."))
                : new ValueTask<ProjectDescriptorOperationResult>(
                    OpenHandler(cancellationToken));
    }
}

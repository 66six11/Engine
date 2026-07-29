using System;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectOpenSessionSnapshotSourceTests
{
    [Fact]
    public void Source_starts_with_no_project_snapshot()
    {
        var source = new ProjectOpenSessionSnapshotSource();

        Assert.Same(ProjectOpenSessionSnapshot.NoProject, source.Current);
    }

    [Fact]
    public void Publish_replaces_snapshot_and_notifies_subscribers()
    {
        var source = new ProjectOpenSessionSnapshotSource();
        var notifications = 0;
        source.SnapshotChanged += (_, _) => notifications++;
        var snapshot = CreateReadySnapshot();

        source.Publish(snapshot);

        Assert.Same(snapshot, source.Current);
        Assert.Equal(1, notifications);
    }

    [Fact]
    public void Publishing_same_snapshot_reference_does_not_notify()
    {
        var snapshot = CreateReadySnapshot();
        var source = new ProjectOpenSessionSnapshotSource(snapshot);
        var notifications = 0;
        source.SnapshotChanged += (_, _) => notifications++;

        source.Publish(snapshot);

        Assert.Equal(0, notifications);
    }

    [Fact]
    public void Publish_rejects_null_snapshot()
    {
        var source = new ProjectOpenSessionSnapshotSource();

        Assert.Throws<ArgumentNullException>(() => source.Publish(null!));
    }

    private static ProjectOpenSessionSnapshot CreateReadySnapshot() =>
        new(
            ProjectOpenSessionState.Ready,
            ProjectOpenNextAction.ActivateProjectProfile,
            new ProjectOpenSummarySnapshot(
                "Example",
                Guid.Parse("7b535774-005d-47ff-90d7-83165df8bac8"),
                assetSourceRootCount: 1));
}

using System;
using Asharia.Editor.Tasks;
using Asharia.Studio.Application.Tasks;
using Xunit;

namespace Asharia.Studio.Application.Tests.Tasks;

public sealed class EditorBackgroundTaskServiceTests
{
    [Fact]
    public void Start_publishes_running_snapshot()
    {
        var service = new EditorBackgroundTaskService();
        var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);

        var snapshot = service.GetSnapshot(id);

        Assert.Equal(EditorBackgroundTaskState.Running, snapshot.State);
        Assert.Equal("Loading Scene", snapshot.Title);
    }

    [Fact]
    public void Start_raises_tasks_changed()
    {
        var service = new EditorBackgroundTaskService();
        var changeCount = 0;
        service.TasksChanged += (_, _) => changeCount++;

        service.Start("project.open", "Opening Project", canCancel: false);

        Assert.Equal(1, changeCount);
    }

    [Fact]
    public void Complete_publishes_completed_snapshot()
    {
        var service = new EditorBackgroundTaskService();
        var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);

        service.Complete(id, "Loaded");

        Assert.Equal(EditorBackgroundTaskState.Completed, service.GetSnapshot(id).State);
        Assert.Equal("Loaded", service.GetSnapshot(id).Message);
    }

    [Fact]
    public void Report_rejects_completed_task_without_changing_snapshot()
    {
        var service = new EditorBackgroundTaskService();
        var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);
        service.Complete(id, "Loaded");

        Assert.Throws<InvalidOperationException>(
            () => service.Report(id, progress: 0.5, message: "Still loading"));

        var snapshot = service.GetSnapshot(id);
        Assert.Equal(EditorBackgroundTaskState.Completed, snapshot.State);
        Assert.Equal("Loaded", snapshot.Message);
    }

    [Fact]
    public void Fail_rejects_completed_task_without_changing_snapshot()
    {
        var service = new EditorBackgroundTaskService();
        var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);
        service.Complete(id, "Loaded");

        Assert.Throws<InvalidOperationException>(
            () => service.Fail(id, "Load failed"));

        var snapshot = service.GetSnapshot(id);
        Assert.Equal(EditorBackgroundTaskState.Completed, snapshot.State);
        Assert.Equal("Loaded", snapshot.Message);
    }
}

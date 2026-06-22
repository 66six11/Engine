using System;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Services;
using Xunit;

namespace Editor.Tests.Shell.Services;

public sealed class EditorLifecycleEventServiceTests
{
    [Fact]
    public void Publish_records_snapshot_and_raises_events_changed()
    {
        var service = new EditorLifecycleEventService();
        var changeCount = 0;
        service.EventsChanged += (_, _) => changeCount++;

        var snapshot = service.Publish(
            EditorLifecycleEventKind.ApplicationOpened,
            "main-window",
            "Opened");

        Assert.Equal(1, changeCount);
        Assert.Equal(1, snapshot.Sequence);
        Assert.Equal(EditorLifecycleEventKind.ApplicationOpened, snapshot.Kind);
        Assert.Equal("main-window", snapshot.Source);
        Assert.Equal("Opened", snapshot.Message);
        Assert.True(snapshot.OccurredAtUtc <= DateTimeOffset.UtcNow);
        Assert.Equal(snapshot, Assert.Single(service.GetRecentEvents()));
    }

    [Fact]
    public void Publish_rejects_blank_source()
    {
        var service = new EditorLifecycleEventService();

        var exception = Assert.Throws<ArgumentException>(
            () => service.Publish(EditorLifecycleEventKind.ApplicationOpened, " "));

        Assert.Equal("source", exception.ParamName);
        Assert.Empty(service.GetRecentEvents());
    }

    [Fact]
    public void Recent_events_keep_latest_100_snapshots()
    {
        var service = new EditorLifecycleEventService();

        for (var index = 0; index < 105; index++)
        {
            service.Publish(EditorLifecycleEventKind.HostActivated, "main-window", index.ToString());
        }

        var events = service.GetRecentEvents();

        Assert.Equal(100, events.Count);
        Assert.Equal(6, events[0].Sequence);
        Assert.Equal("5", events[0].Message);
        Assert.Equal(105, events[^1].Sequence);
        Assert.Equal("104", events[^1].Message);
        Assert.Equal(Enumerable.Range(6, 100).Select(static value => (long)value), events.Select(static snapshot => snapshot.Sequence));
    }

    [Fact]
    public void GetRecentEvents_returns_snapshot_copy()
    {
        var service = new EditorLifecycleEventService();
        service.Publish(EditorLifecycleEventKind.HostActivated, "main-window");

        var firstRead = service.GetRecentEvents();
        service.Publish(EditorLifecycleEventKind.HostDeactivated, "main-window");

        Assert.Single(firstRead);
        Assert.Equal(2, service.GetRecentEvents().Count);
    }
}

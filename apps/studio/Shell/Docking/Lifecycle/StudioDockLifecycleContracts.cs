using System;
using System.Collections.Generic;

namespace Editor.Shell.Docking.Lifecycle;

public enum EditorLifecycleEventKind
{
    FloatingWindowOpened,
    FloatingWindowClosed,
    FloatingWindowActivated,
    FloatingWindowDeactivated,
}

public sealed record EditorLifecycleEventSnapshot(
    long Sequence,
    EditorLifecycleEventKind Kind,
    string Source,
    string? Message,
    DateTimeOffset OccurredAtUtc);

public interface IEditorLifecycleEventService
{
    event EventHandler? EventsChanged;
    EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null);
    IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents();
}

internal sealed class EditorLifecycleEventService : IEditorLifecycleEventService
{
    private const int RecentEventCapacity = 100;
    private readonly List<EditorLifecycleEventSnapshot> events_ = [];
    private long nextSequence_;

    public event EventHandler? EventsChanged;

    public EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        var snapshot = new EditorLifecycleEventSnapshot(
            ++nextSequence_, kind, source, message, DateTimeOffset.UtcNow);
        events_.Add(snapshot);
        if (events_.Count > RecentEventCapacity)
        {
            events_.RemoveRange(0, events_.Count - RecentEventCapacity);
        }
        EventsChanged?.Invoke(this, EventArgs.Empty);
        return snapshot;
    }

    public IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents() =>
        events_.ToArray();
}

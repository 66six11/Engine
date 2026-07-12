using System;
using System.Collections.Generic;
using Asharia.Editor.Lifecycle;

namespace Asharia.Studio.Application.Lifecycle;

public sealed class EditorLifecycleEventService : IEditorLifecycleEventService
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
            ++nextSequence_,
            kind,
            source,
            message,
            DateTimeOffset.UtcNow);
        events_.Add(snapshot);
        if (events_.Count > RecentEventCapacity)
        {
            events_.RemoveRange(0, events_.Count - RecentEventCapacity);
        }

        EventsChanged?.Invoke(this, EventArgs.Empty);
        return snapshot;
    }

    public IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents()
    {
        return events_.ToArray();
    }
}

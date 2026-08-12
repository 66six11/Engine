using System;
using System.Collections.Generic;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Projects;

internal sealed record SceneEditHistoryEntry(
    Guid SceneId,
    Guid ObjectId,
    string Label,
    ProjectEditId InteractionId,
    TransformValue BeforeTransform,
    TransformValue AfterTransform,
    ContentStateId BeforeContentStateId,
    ContentStateId AfterContentStateId,
    long EstimatedBytes);

internal sealed class SceneEditHistory
{
    internal const int DefaultEntryLimit = 256;
    internal const long DefaultByteLimit = 16L * 1024L * 1024L;
    internal const long TransformEntryEstimatedBytes = 256;

    private readonly int entryLimit_;
    private readonly long byteLimit_;
    private readonly List<SceneEditHistoryEntry> entries_ = [];
    private int cursor_;
    private long estimatedBytes_;

    public SceneEditHistory(
        int entryLimit = DefaultEntryLimit,
        long byteLimit = DefaultByteLimit)
    {
        if (entryLimit <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(entryLimit),
                "The history entry limit must be positive.");
        }
        if (byteLimit <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(byteLimit),
                "The history byte limit must be positive.");
        }

        entryLimit_ = entryLimit;
        byteLimit_ = byteLimit;
    }

    public int Count => entries_.Count;

    public int Cursor => cursor_;

    public long EstimatedBytes => estimatedBytes_;

    public bool CanUndo => cursor_ > 0;

    public bool CanRedo => cursor_ < entries_.Count;

    public SceneEditHistoryEntry? UndoCandidate =>
        CanUndo ? entries_[cursor_ - 1] : null;

    public SceneEditHistoryEntry? RedoCandidate =>
        CanRedo ? entries_[cursor_] : null;

    public string? UndoLabel => UndoCandidate?.Label;

    public string? RedoLabel => RedoCandidate?.Label;

    public void Reset()
    {
        entries_.Clear();
        cursor_ = 0;
        estimatedBytes_ = 0;
    }

    public void Commit(SceneEditHistoryEntry entry)
    {
        Validate(entry);
        if (entry.EstimatedBytes > byteLimit_)
        {
            throw new ArgumentOutOfRangeException(
                nameof(entry),
                "A single scene edit history entry cannot exceed the history byte limit.");
        }
        TruncateRedoTail();

        entries_.Add(entry);
        cursor_++;
        estimatedBytes_ = checked(estimatedBytes_ + entry.EstimatedBytes);

        while (entries_.Count > entryLimit_ || estimatedBytes_ > byteLimit_)
        {
            var oldest = entries_[0];
            entries_.RemoveAt(0);
            cursor_--;
            estimatedBytes_ -= oldest.EstimatedBytes;
        }
    }

    public void CommitUndo(SceneEditHistoryEntry expectedEntry)
    {
        if (!ReferenceEquals(UndoCandidate, expectedEntry))
        {
            throw new InvalidOperationException(
                "The Undo candidate changed before the authoritative edit completed.");
        }

        cursor_--;
    }

    public void CommitRedo(SceneEditHistoryEntry expectedEntry)
    {
        if (!ReferenceEquals(RedoCandidate, expectedEntry))
        {
            throw new InvalidOperationException(
                "The Redo candidate changed before the authoritative edit completed.");
        }

        cursor_++;
    }

    private void TruncateRedoTail()
    {
        for (var index = entries_.Count - 1; index >= cursor_; index--)
        {
            estimatedBytes_ -= entries_[index].EstimatedBytes;
            entries_.RemoveAt(index);
        }
    }

    private static void Validate(SceneEditHistoryEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        if (entry.SceneId == Guid.Empty)
        {
            throw new ArgumentException(
                "A scene edit history entry requires a stable scene id.",
                nameof(entry));
        }
        if (entry.ObjectId == Guid.Empty)
        {
            throw new ArgumentException(
                "A scene edit history entry requires a stable object id.",
                nameof(entry));
        }
        if (string.IsNullOrWhiteSpace(entry.Label))
        {
            throw new ArgumentException(
                "A scene edit history entry requires a label.",
                nameof(entry));
        }
        if (!entry.InteractionId.IsValid)
        {
            throw new ArgumentException(
                "A scene edit history entry requires a valid interaction id.",
                nameof(entry));
        }
        if (!entry.BeforeContentStateId.IsValid || !entry.AfterContentStateId.IsValid ||
            entry.BeforeContentStateId == entry.AfterContentStateId)
        {
            throw new ArgumentException(
                "A scene edit history entry requires distinct valid content state ids.",
                nameof(entry));
        }
        if (entry.EstimatedBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(entry),
                "A scene edit history entry requires a positive byte estimate.");
        }
    }
}

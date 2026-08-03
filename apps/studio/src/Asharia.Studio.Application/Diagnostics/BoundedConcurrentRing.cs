using System;
using System.Collections.Immutable;
using System.Threading;

namespace Asharia.Studio.Application.Diagnostics;

internal sealed class BoundedConcurrentRing<T>
    where T : class
{
    private readonly object?[] slots_;
    private readonly Func<T, long> sequenceSelector_;
    private long nextSequence_;
    private long highestCompletedSequence_;
    private long publicationVersion_;

    public BoundedConcurrentRing(
        int capacity,
        Func<T, long> sequenceSelector)
    {
        if (capacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }

        ArgumentNullException.ThrowIfNull(sequenceSelector);
        slots_ = new object[capacity];
        sequenceSelector_ = sequenceSelector;
    }

    public int Capacity => slots_.Length;

    public long PublicationVersion => Volatile.Read(ref publicationVersion_);

    public long DroppedCount => CountDropped(
        Volatile.Read(ref highestCompletedSequence_));

    public T Publish(Func<long, T> createRecord)
    {
        ArgumentNullException.ThrowIfNull(createRecord);

        var sequence = Interlocked.Increment(ref nextSequence_);
        T record;
        try
        {
            record = createRecord(sequence);
            if (sequenceSelector_(record) != sequence)
            {
                throw new InvalidOperationException(
                    "A bounded ring record must retain its reserved sequence.");
            }
        }
        catch
        {
            Commit(sequence, new DroppedSequence(sequence));
            ObserveCompletedSequence(sequence);
            Interlocked.Increment(ref publicationVersion_);
            throw;
        }

        Commit(sequence, record);
        ObserveCompletedSequence(sequence);
        Interlocked.Increment(ref publicationVersion_);
        return record;
    }

    private void Commit(
        long sequence,
        object candidate)
    {
        var index = SlotIndex(sequence);
        while (true)
        {
            var observed = Volatile.Read(ref slots_[index]);
            if (observed is not null
                && SequenceOf(observed) >= sequence)
            {
                return;
            }

            if (ReferenceEquals(
                    Interlocked.CompareExchange(
                        ref slots_[index],
                        candidate,
                        observed),
                    observed))
            {
                return;
            }
        }
    }

    private void ObserveCompletedSequence(long sequence)
    {
        var observed = Volatile.Read(ref highestCompletedSequence_);
        while (observed < sequence)
        {
            var exchanged = Interlocked.CompareExchange(
                ref highestCompletedSequence_,
                sequence,
                observed);
            if (exchanged == observed)
            {
                return;
            }

            observed = exchanged;
        }
    }

    public StudioCursorWindow<T> Read(
        long afterSequence,
        int maxCount,
        Predicate<T>? predicate = null)
    {
        if (afterSequence < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(afterSequence));
        }

        if (maxCount <= 0 || maxCount > Capacity)
        {
            throw new ArgumentOutOfRangeException(nameof(maxCount));
        }

        var completed = Volatile.Read(ref highestCompletedSequence_);
        var reserved = Volatile.Read(ref nextSequence_);
        var oldest = Math.Max(1, completed - Capacity + 1);
        var cursorExpired = afterSequence < oldest - 1;
        var nextCursor = afterSequence;
        var items = ImmutableArray.CreateBuilder<T>(Math.Min(maxCount, Capacity));
        var truncated = false;

        if (afterSequence < completed)
        {
            var sequence = Math.Max(afterSequence + 1, oldest);
            while (true)
            {
                var entry = Volatile.Read(ref slots_[SlotIndex(sequence)]);
                if (entry is null || SequenceOf(entry) != sequence)
                {
                    truncated = true;
                    break;
                }

                nextCursor = sequence;
                if (entry is T record
                    && (predicate is null || predicate(record)))
                {
                    items.Add(record);
                    if (items.Count == maxCount)
                    {
                        truncated = sequence < completed;
                        break;
                    }
                }

                if (sequence == completed)
                {
                    break;
                }

                sequence++;
            }
        }

        truncated |= reserved > nextCursor;

        return new StudioCursorWindow<T>(
            oldest,
            nextCursor,
            CountDropped(completed),
            cursorExpired,
            truncated,
            items.ToImmutable());
    }

    public T? GetLatest()
    {
        var completed = Volatile.Read(ref highestCompletedSequence_);
        var oldest = Math.Max(1, completed - Capacity + 1);
        for (var sequence = completed; sequence >= oldest; sequence--)
        {
            var entry = Volatile.Read(ref slots_[SlotIndex(sequence)]);
            if (entry is T record && sequenceSelector_(record) == sequence)
            {
                return record;
            }
        }

        return null;
    }

    private long CountDropped(long completed)
    {
        if (completed == 0)
        {
            return 0;
        }

        var oldest = Math.Max(1, completed - Capacity + 1);
        var dropped = Math.Max(0, completed - Capacity);
        var sequence = oldest;
        while (true)
        {
            var entry = Volatile.Read(ref slots_[SlotIndex(sequence)]);
            if ((entry is DroppedSequence droppedSequence
                    && droppedSequence.Sequence == sequence)
                || (entry is not null && SequenceOf(entry) > sequence))
            {
                dropped++;
            }

            if (sequence == completed)
            {
                return dropped;
            }

            sequence++;
        }
    }

    private int SlotIndex(long sequence) =>
        (int)((sequence - 1) % Capacity);

    private long SequenceOf(object entry) =>
        entry is DroppedSequence dropped
            ? dropped.Sequence
            : sequenceSelector_((T)entry);

    private sealed record DroppedSequence(long Sequence);
}

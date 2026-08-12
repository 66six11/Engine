using System;
using System.Collections.Immutable;
using System.Threading;

namespace Asharia.Studio.Application.Diagnostics;

internal readonly record struct BoundedConcurrentRingState(
    int CountCapacity,
    long PayloadByteCapacity,
    int ResidentCount,
    long EstimatedResidentPayloadBytes,
    long TotalDropped);

internal sealed class BoundedConcurrentRing<T>
    where T : class
{
    private readonly object gate_ = new();
    private readonly object?[] slots_;
    private readonly long[] residentByteCounts_;
    private readonly Func<T, long> sequenceSelector_;
    private readonly Func<T, long> byteCountSelector_;
    private readonly long byteCapacity_;
    private long nextSequence_;
    private long highestCompletedSequence_;
    private long retentionFloor_ = 1;
    private long publicationVersion_;
    private long residentBytes_;
    private int residentCount_;

    public BoundedConcurrentRing(
        int capacity,
        Func<T, long> sequenceSelector)
        : this(
            capacity,
            long.MaxValue,
            sequenceSelector,
            static _ => 0)
    {
    }

    public BoundedConcurrentRing(
        int capacity,
        long byteCapacity,
        Func<T, long> sequenceSelector,
        Func<T, long> byteCountSelector)
    {
        if (capacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }

        if (byteCapacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(byteCapacity));
        }

        ArgumentNullException.ThrowIfNull(sequenceSelector);
        ArgumentNullException.ThrowIfNull(byteCountSelector);
        slots_ = new object[capacity];
        residentByteCounts_ = new long[capacity];
        byteCapacity_ = byteCapacity;
        sequenceSelector_ = sequenceSelector;
        byteCountSelector_ = byteCountSelector;
    }

    public int Capacity => slots_.Length;

    public long PublicationVersion => Volatile.Read(ref publicationVersion_);

    public long DroppedCount => GetState().TotalDropped;

    public BoundedConcurrentRingState GetState()
    {
        lock (gate_)
        {
            return new BoundedConcurrentRingState(
                Capacity,
                byteCapacity_,
                residentCount_,
                residentBytes_,
                CountDropped());
        }
    }

    public T Publish(Func<long, T> createRecord)
    {
        ArgumentNullException.ThrowIfNull(createRecord);

        var sequence = Interlocked.Increment(ref nextSequence_);
        T record;
        long byteCount;
        try
        {
            record = createRecord(sequence);
            if (sequenceSelector_(record) != sequence)
            {
                throw new InvalidOperationException(
                    "A bounded ring record must retain its reserved sequence.");
            }

            byteCount = byteCountSelector_(record);
            if (byteCount < 0)
            {
                throw new InvalidOperationException(
                    "A bounded ring record byte count cannot be negative.");
            }
        }
        catch
        {
            Complete(sequence, record: null, byteCount: 0);
            throw;
        }

        Complete(sequence, record, byteCount);
        return record;
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

        lock (gate_)
        {
            var completed = highestCompletedSequence_;
            var reserved = Volatile.Read(ref nextSequence_);
            var oldest = retentionFloor_;
            var cursorExpired = afterSequence < oldest - 1;
            var nextCursor = afterSequence;
            var items = ImmutableArray.CreateBuilder<T>(Math.Min(maxCount, Capacity));
            var truncated = false;

            if (afterSequence < completed)
            {
                var sequence = Math.Max(afterSequence + 1, oldest);
                while (true)
                {
                    var entry = slots_[SlotIndex(sequence)];
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
                CountDropped(),
                cursorExpired,
                truncated,
                items.ToImmutable());
        }
    }

    public T? GetLatest()
    {
        lock (gate_)
        {
            for (var sequence = highestCompletedSequence_;
                 sequence >= retentionFloor_;
                 sequence--)
            {
                var entry = slots_[SlotIndex(sequence)];
                if (entry is T record && sequenceSelector_(record) == sequence)
                {
                    return record;
                }
            }

            return null;
        }
    }

    private void Complete(
        long sequence,
        T? record,
        long byteCount)
    {
        lock (gate_)
        {
            if (sequence > highestCompletedSequence_)
            {
                highestCompletedSequence_ = sequence;
                AdvanceRetentionFloor(Math.Max(
                    1,
                    highestCompletedSequence_ - Capacity + 1));
            }

            if (sequence >= retentionFloor_)
            {
                if (record is null || byteCount > byteCapacity_)
                {
                    Commit(sequence, new DroppedSequence(sequence), byteCount: 0);
                }
                else
                {
                    Commit(sequence, record, byteCount);
                    EnforceByteCapacity();
                }
            }

            publicationVersion_++;
        }
    }

    private void Commit(
        long sequence,
        object candidate,
        long byteCount)
    {
        var index = SlotIndex(sequence);
        var observed = slots_[index];
        if (observed is not null && SequenceOf(observed) >= sequence)
        {
            return;
        }

        RemoveResident(index);
        slots_[index] = candidate;
        if (candidate is T)
        {
            residentByteCounts_[index] = byteCount;
            residentBytes_ += byteCount;
            residentCount_++;
        }
    }

    private void EnforceByteCapacity()
    {
        if (residentBytes_ <= byteCapacity_)
        {
            return;
        }

        var sequence = retentionFloor_;
        while (residentBytes_ > byteCapacity_
               && sequence <= highestCompletedSequence_)
        {
            var index = SlotIndex(sequence);
            var entry = slots_[index];
            if (entry is T && SequenceOf(entry) == sequence)
            {
                AdvanceRetentionFloor(sequence + 1);
                sequence = retentionFloor_;
                continue;
            }

            sequence++;
        }
    }

    private void AdvanceRetentionFloor(long requestedFloor)
    {
        if (requestedFloor <= retentionFloor_)
        {
            return;
        }

        var distance = requestedFloor - retentionFloor_;
        if (distance >= Capacity)
        {
            for (var index = 0; index < slots_.Length; index++)
            {
                var entry = slots_[index];
                if (entry is not null && SequenceOf(entry) < requestedFloor)
                {
                    ClearSlot(index);
                }
            }
        }
        else
        {
            for (var sequence = retentionFloor_;
                 sequence < requestedFloor;
                 sequence++)
            {
                var index = SlotIndex(sequence);
                var entry = slots_[index];
                if (entry is not null && SequenceOf(entry) == sequence)
                {
                    ClearSlot(index);
                }
            }
        }

        retentionFloor_ = requestedFloor;
    }

    private void ClearSlot(int index)
    {
        RemoveResident(index);
        slots_[index] = null;
    }

    private void RemoveResident(int index)
    {
        if (slots_[index] is not T)
        {
            residentByteCounts_[index] = 0;
            return;
        }

        residentBytes_ -= residentByteCounts_[index];
        residentByteCounts_[index] = 0;
        residentCount_--;
    }

    private long CountDropped()
    {
        if (highestCompletedSequence_ == 0)
        {
            return 0;
        }

        var dropped = retentionFloor_ - 1;
        for (var sequence = retentionFloor_;
             sequence <= highestCompletedSequence_;
             sequence++)
        {
            var entry = slots_[SlotIndex(sequence)];
            if (entry is DroppedSequence droppedSequence
                && droppedSequence.Sequence == sequence)
            {
                dropped++;
            }
        }

        return dropped;
    }

    private int SlotIndex(long sequence) =>
        (int)((sequence - 1) % Capacity);

    private long SequenceOf(object entry) =>
        entry is DroppedSequence dropped
            ? dropped.Sequence
            : sequenceSelector_((T)entry);

    private sealed record DroppedSequence(long Sequence);
}

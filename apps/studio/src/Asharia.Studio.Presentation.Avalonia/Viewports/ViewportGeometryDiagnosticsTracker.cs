using System;
using System.Collections.Generic;
using System.Diagnostics;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public readonly struct ViewportResizeMeasurementToken
{
    internal ViewportResizeMeasurementToken(
        long startedTimestamp,
        ulong baselineGeneration,
        ulong resetEpoch,
        ulong minimumRecordSequence,
        ulong minimumVisibilitySequence,
        bool hiddenAtStart)
    {
        StartedTimestamp = startedTimestamp;
        BaselineGeneration = baselineGeneration;
        ResetEpoch = resetEpoch;
        MinimumRecordSequence = minimumRecordSequence;
        MinimumVisibilitySequence = minimumVisibilitySequence;
        HiddenAtStart = hiddenAtStart;
    }

    internal long StartedTimestamp { get; }

    internal ulong BaselineGeneration { get; }

    internal ulong ResetEpoch { get; }

    internal ulong MinimumRecordSequence { get; }

    internal ulong MinimumVisibilitySequence { get; }

    internal bool HiddenAtStart { get; }
}

public readonly record struct ViewportResizePresentationMetrics(
    int ObservedBoundsGenerations,
    int UniqueExactSubmittedGenerations,
    int UniqueExactCompletedGenerations,
    TimeSpan WindowElapsed,
    TimeSpan P95UniqueCompletionInterval,
    TimeSpan MaximumUniqueCompletionInterval,
    TimeSpan P95BoundsToExactSubmit,
    TimeSpan MaximumBoundsToExactSubmit,
    TimeSpan P95BoundsToExactCompletion,
    TimeSpan MaximumBoundsToExactCompletion,
    TimeSpan RequestedMismatchHiddenDuration,
    ulong FinalGeometryGeneration,
    bool FinalGenerationHasExactSurface,
    bool FinalGenerationCompleted,
    bool ContainsNonBoundsGeometryChanges,
    bool TrackerResetSinceMeasurement,
    bool RingOverflowed)
{
    public double UniqueExactSubmittedPerSecond => WindowElapsed <= TimeSpan.Zero
        ? 0
        : UniqueExactSubmittedGenerations / WindowElapsed.TotalSeconds;

    public double UniqueExactCompletedPerSecond => WindowElapsed <= TimeSpan.Zero
        ? 0
        : UniqueExactCompletedGenerations / WindowElapsed.TotalSeconds;

    public double CompletionCoverage => ObservedBoundsGenerations == 0
        ? 0
        : (double)UniqueExactCompletedGenerations / ObservedBoundsGenerations;

    public double RequestedMismatchHiddenDutyCycle => WindowElapsed <= TimeSpan.Zero
        ? 0
        : Math.Clamp(
            RequestedMismatchHiddenDuration.TotalSeconds / WindowElapsed.TotalSeconds,
            0,
            1);
}

internal enum ViewportGeometryChangeSource
{
    Attachment,
    Bounds,
    Scaling,
    PresentationIdentity,
}

internal sealed class ViewportGeometryDiagnosticsTracker
{
    internal const int RecordCapacity = 256;
    private const int VisibilityCapacity = 512;

    private sealed class GenerationRecord
    {
        public ulong Sequence { get; init; }

        public ulong Generation { get; init; }

        public ViewportExtent Extent { get; init; }

        public ViewportGeometryChangeSource Source { get; init; }

        public long ObservedTimestamp { get; init; }

        public long? FirstExactSubmittedTimestamp { get; set; }

        public long? FirstExactCompletedTimestamp { get; set; }
    }

    private readonly record struct VisibilityTransition(
        ulong Sequence,
        long Timestamp,
        bool IsHidden);

    private readonly record struct GenerationSnapshot(
        ulong Sequence,
        ulong Generation,
        ViewportGeometryChangeSource Source,
        long ObservedTimestamp,
        long? FirstExactSubmittedTimestamp,
        long? FirstExactCompletedTimestamp);

    private readonly object gate_ = new();
    private readonly GenerationRecord?[] records_ = new GenerationRecord[RecordCapacity];
    private readonly VisibilityTransition[] visibility_ =
        new VisibilityTransition[VisibilityCapacity];
    private int recordCount_;
    private int nextRecordIndex_;
    private int visibilityCount_;
    private int nextVisibilityIndex_;
    private ulong nextRecordSequence_;
    private ulong nextVisibilitySequence_;
    private ulong resetEpoch_;
    private bool isHidden_ = true;

    public void Reset()
    {
        lock (gate_)
        {
            Array.Clear(records_);
            Array.Clear(visibility_);
            recordCount_ = 0;
            nextRecordIndex_ = 0;
            visibilityCount_ = 0;
            nextVisibilityIndex_ = 0;
            nextRecordSequence_ = 0;
            nextVisibilitySequence_ = 0;
            resetEpoch_ = checked(resetEpoch_ + 1);
            isHidden_ = true;
        }
    }

    public void RecordGeneration(
        ulong generation,
        ViewportExtent extent,
        ViewportGeometryChangeSource source,
        long observedTimestamp)
    {
        lock (gate_)
        {
            records_[nextRecordIndex_] = new GenerationRecord
            {
                Sequence = checked(++nextRecordSequence_),
                Generation = generation,
                Extent = extent,
                Source = source,
                ObservedTimestamp = observedTimestamp,
            };
            nextRecordIndex_ = (nextRecordIndex_ + 1) % RecordCapacity;
            recordCount_ = Math.Min(recordCount_ + 1, RecordCapacity);
        }
    }

    public void MarkExactSurfaceSubmitted(ulong generation, long submittedTimestamp)
    {
        lock (gate_)
        {
            var record = FindGenerationLocked(generation);
            if (record is null)
            {
                return;
            }
            record.FirstExactSubmittedTimestamp ??= submittedTimestamp;
        }
    }

    public void MarkRequestedVisualHidden(bool isHidden, long timestamp)
    {
        lock (gate_)
        {
            SetHiddenLocked(isHidden, timestamp);
        }
    }

    public void MarkExactSurfaceCompleted(ulong generation, long completedTimestamp)
    {
        lock (gate_)
        {
            var record = FindGenerationLocked(generation);
            if (record is not null)
            {
                record.FirstExactCompletedTimestamp ??= completedTimestamp;
            }
        }
    }

    public ViewportResizeMeasurementToken BeginMeasurement(
        ulong baselineGeneration,
        long startedTimestamp)
    {
        lock (gate_)
        {
            return new ViewportResizeMeasurementToken(
                startedTimestamp,
                baselineGeneration,
                resetEpoch_,
                checked(nextRecordSequence_ + 1),
                checked(nextVisibilitySequence_ + 1),
                isHidden_);
        }
    }

    public ViewportResizePresentationMetrics Capture(
        ViewportResizeMeasurementToken token,
        ulong finalGeometryGeneration,
        bool finalGenerationHasExactSurface,
        long capturedTimestamp)
    {
        lock (gate_)
        {
            if (token.ResetEpoch != resetEpoch_)
            {
                return new ViewportResizePresentationMetrics(
                    ObservedBoundsGenerations: 0,
                    UniqueExactSubmittedGenerations: 0,
                    UniqueExactCompletedGenerations: 0,
                    WindowElapsed: TimeSpan.Zero,
                    P95UniqueCompletionInterval: TimeSpan.Zero,
                    MaximumUniqueCompletionInterval: TimeSpan.Zero,
                    P95BoundsToExactSubmit: TimeSpan.Zero,
                    MaximumBoundsToExactSubmit: TimeSpan.Zero,
                    P95BoundsToExactCompletion: TimeSpan.Zero,
                    MaximumBoundsToExactCompletion: TimeSpan.Zero,
                    RequestedMismatchHiddenDuration: TimeSpan.Zero,
                    FinalGeometryGeneration: finalGeometryGeneration,
                    FinalGenerationHasExactSurface: false,
                    FinalGenerationCompleted: false,
                    ContainsNonBoundsGeometryChanges: false,
                    TrackerResetSinceMeasurement: true,
                    RingOverflowed: false);
            }
            var records = SnapshotRecordsLocked();
            var visibility = SnapshotVisibilityLocked();
            var recordOverflowed =
                nextRecordSequence_ >= token.MinimumRecordSequence &&
                records.Count != 0 &&
                records[0].Sequence > token.MinimumRecordSequence;
            var visibilityOverflowed =
                nextVisibilitySequence_ >= token.MinimumVisibilitySequence &&
                visibility.Count != 0 &&
                visibility[0].Sequence > token.MinimumVisibilitySequence;

            var boundsRecords = new List<GenerationSnapshot>();
            GenerationSnapshot? finalRecord = null;
            var containsNonBoundsGeometryChanges = false;
            foreach (var record in records)
            {
                if (record.Sequence < token.MinimumRecordSequence ||
                    record.Generation <= token.BaselineGeneration)
                {
                    continue;
                }
                if (record.Generation == finalGeometryGeneration)
                {
                    finalRecord = record;
                }
                if (record.Source == ViewportGeometryChangeSource.Bounds)
                {
                    boundsRecords.Add(record);
                }
                else
                {
                    containsNonBoundsGeometryChanges = true;
                }
            }

            boundsRecords.Sort(static (left, right) =>
                left.ObservedTimestamp.CompareTo(right.ObservedTimestamp));
            var firstObserved = boundsRecords.Count == 0
                ? token.StartedTimestamp
                : boundsRecords[0].ObservedTimestamp;
            var finalCompletedTimestamp = finalRecord?.FirstExactCompletedTimestamp;
            var windowEnd = Math.Max(
                firstObserved,
                finalCompletedTimestamp ?? capturedTimestamp);

            var submittedLatencies = new List<TimeSpan>();
            var completedLatencies = new List<TimeSpan>();
            var completedTimestamps = new List<long>();
            var submittedCount = 0;
            var completedCount = 0;
            foreach (var record in boundsRecords)
            {
                if (record.FirstExactSubmittedTimestamp is { } submitted)
                {
                    submittedCount++;
                    submittedLatencies.Add(Elapsed(record.ObservedTimestamp, submitted));
                }
                if (record.FirstExactCompletedTimestamp is { } completed)
                {
                    completedCount++;
                    completedLatencies.Add(Elapsed(record.ObservedTimestamp, completed));
                    completedTimestamps.Add(completed);
                }
            }

            completedTimestamps.Sort();
            var completionIntervals = new List<TimeSpan>();
            for (var index = 1; index < completedTimestamps.Count; index++)
            {
                completionIntervals.Add(Elapsed(
                    completedTimestamps[index - 1],
                    completedTimestamps[index]));
            }

            var hiddenDuration = ComputeHiddenDuration(
                token,
                visibility,
                firstObserved,
                windowEnd);
            return new ViewportResizePresentationMetrics(
                boundsRecords.Count,
                submittedCount,
                completedCount,
                Elapsed(firstObserved, windowEnd),
                Percentile95(completionIntervals),
                Maximum(completionIntervals),
                Percentile95(submittedLatencies),
                Maximum(submittedLatencies),
                Percentile95(completedLatencies),
                Maximum(completedLatencies),
                hiddenDuration,
                finalGeometryGeneration,
                finalGenerationHasExactSurface,
                finalCompletedTimestamp is not null,
                containsNonBoundsGeometryChanges,
                TrackerResetSinceMeasurement: false,
                RingOverflowed: recordOverflowed || visibilityOverflowed);
        }
    }

    private GenerationRecord? FindGenerationLocked(ulong generation)
    {
        for (var offset = 0; offset < recordCount_; offset++)
        {
            var index = (nextRecordIndex_ - 1 - offset + RecordCapacity) % RecordCapacity;
            if (records_[index] is { } record && record.Generation == generation)
            {
                return record;
            }
        }
        return null;
    }

    private void SetHiddenLocked(bool isHidden, long timestamp)
    {
        if (isHidden_ == isHidden)
        {
            return;
        }
        isHidden_ = isHidden;
        visibility_[nextVisibilityIndex_] = new VisibilityTransition(
            checked(++nextVisibilitySequence_),
            timestamp,
            isHidden);
        nextVisibilityIndex_ = (nextVisibilityIndex_ + 1) % VisibilityCapacity;
        visibilityCount_ = Math.Min(visibilityCount_ + 1, VisibilityCapacity);
    }

    private List<GenerationSnapshot> SnapshotRecordsLocked()
    {
        var result = new List<GenerationSnapshot>(recordCount_);
        var first = (nextRecordIndex_ - recordCount_ + RecordCapacity) % RecordCapacity;
        for (var offset = 0; offset < recordCount_; offset++)
        {
            var record = records_[(first + offset) % RecordCapacity]!;
            result.Add(new GenerationSnapshot(
                record.Sequence,
                record.Generation,
                record.Source,
                record.ObservedTimestamp,
                record.FirstExactSubmittedTimestamp,
                record.FirstExactCompletedTimestamp));
        }
        return result;
    }

    private List<VisibilityTransition> SnapshotVisibilityLocked()
    {
        var result = new List<VisibilityTransition>(visibilityCount_);
        var first =
            (nextVisibilityIndex_ - visibilityCount_ + VisibilityCapacity) %
            VisibilityCapacity;
        for (var offset = 0; offset < visibilityCount_; offset++)
        {
            result.Add(visibility_[(first + offset) % VisibilityCapacity]);
        }
        return result;
    }

    private static TimeSpan ComputeHiddenDuration(
        ViewportResizeMeasurementToken token,
        IReadOnlyList<VisibilityTransition> transitions,
        long windowStart,
        long windowEnd)
    {
        var isHidden = token.HiddenAtStart;
        foreach (var transition in transitions)
        {
            if (transition.Sequence < token.MinimumVisibilitySequence)
            {
                continue;
            }
            if (transition.Timestamp >= windowStart)
            {
                break;
            }
            isHidden = transition.IsHidden;
        }

        var hiddenTicks = 0L;
        var cursor = windowStart;
        foreach (var transition in transitions)
        {
            if (transition.Sequence < token.MinimumVisibilitySequence ||
                transition.Timestamp < windowStart || transition.Timestamp > windowEnd)
            {
                continue;
            }
            var timestamp = Math.Max(cursor, transition.Timestamp);
            if (isHidden)
            {
                hiddenTicks = checked(hiddenTicks + timestamp - cursor);
            }
            cursor = timestamp;
            isHidden = transition.IsHidden;
        }
        if (isHidden)
        {
            hiddenTicks = checked(hiddenTicks + windowEnd - cursor);
        }
        return StopwatchTicksToTimeSpan(hiddenTicks);
    }

    private static TimeSpan Elapsed(long start, long end) =>
        StopwatchTicksToTimeSpan(Math.Max(0, end - start));

    private static TimeSpan StopwatchTicksToTimeSpan(long ticks) =>
        TimeSpan.FromSeconds((double)ticks / Stopwatch.Frequency);

    private static TimeSpan Percentile95(List<TimeSpan> values)
    {
        if (values.Count == 0)
        {
            return TimeSpan.Zero;
        }
        values.Sort();
        var index = Math.Clamp(
            (int)Math.Ceiling(values.Count * 0.95) - 1,
            0,
            values.Count - 1);
        return values[index];
    }

    private static TimeSpan Maximum(List<TimeSpan> values)
    {
        if (values.Count == 0)
        {
            return TimeSpan.Zero;
        }
        var maximum = values[0];
        for (var index = 1; index < values.Count; index++)
        {
            if (values[index] > maximum)
            {
                maximum = values[index];
            }
        }
        return maximum;
    }
}

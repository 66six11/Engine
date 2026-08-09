using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal readonly record struct ViewportPresentationEndpointId(string Value)
{
    public bool IsValid => !string.IsNullOrWhiteSpace(Value);
}

internal readonly record struct ViewportPresentationTelemetryIdentity(
    ViewportPresentationEndpointId EndpointId,
    ViewportSessionId SessionId,
    ulong Epoch,
    ViewportPresentationTransactionId TransactionId,
    ulong Generation,
    ViewportExtent Extent)
{
    public bool IsValid =>
        EndpointId.IsValid &&
        SessionId.IsValid &&
        TransactionId.IsValid &&
        Extent.Width != 0 &&
        Extent.Height != 0;
}

internal enum ViewportPresentationTelemetryEventKind
{
    Proposed,
    Prepared,
    Published,
    Rendered,
    PhysicalDisplayed,
    HiddenStarted,
    HiddenEnded,
    Stale,
    Superseded,
    Faulted,
    Quarantined,
    CandidateProduced,
    CandidateWasted,
    ResourceSnapshot,
    ResourceReclaimed,
}

internal readonly record struct ViewportPresentationTelemetryEvent(
    ViewportPresentationTelemetryEventKind Kind,
    long Timestamp,
    ViewportPresentationTelemetryIdentity Identity,
    long Amount = 0)
{
    public ViewportPresentationEndpointId EndpointId => Identity.EndpointId;

    public ViewportSessionId SessionId => Identity.SessionId;

    public ulong Epoch => Identity.Epoch;

    public ViewportPresentationTransactionId TransactionId => Identity.TransactionId;

    public ulong Generation => Identity.Generation;

    public ViewportExtent Extent => Identity.Extent;

    public bool IsValid =>
        Timestamp >= 0 &&
        Identity.IsValid &&
        Kind switch
        {
            ViewportPresentationTelemetryEventKind.CandidateProduced or
            ViewportPresentationTelemetryEventKind.CandidateWasted or
            ViewportPresentationTelemetryEventKind.ResourceReclaimed => Amount > 0,
            ViewportPresentationTelemetryEventKind.ResourceSnapshot => Amount >= 0,
            _ => Amount == 0,
        };
}

internal enum ViewportPresentationTelemetryRecordResult
{
    Recorded,
    RejectedInvalidEvent,
}

internal readonly record struct ViewportPresentationLatencyDistribution(
    int SampleCount,
    TimeSpan P50,
    TimeSpan P95,
    TimeSpan Maximum);

internal readonly record struct ViewportPresentationStageLatencyMetrics(
    ViewportPresentationLatencyDistribution ProposedToPrepared,
    ViewportPresentationLatencyDistribution PreparedToPublished,
    ViewportPresentationLatencyDistribution PublishedToRendered,
    ViewportPresentationLatencyDistribution RenderedToPhysicalDisplayed,
    ViewportPresentationLatencyDistribution ProposedToRendered,
    ViewportPresentationLatencyDistribution ProposedToPhysicalDisplayed);

internal readonly record struct ViewportPresentationStageCountMetrics(
    int Proposed,
    int Prepared,
    int Published,
    int Rendered,
    int PhysicalDisplayed);

internal readonly record struct ViewportPresentationVisibilityMetrics(
    TimeSpan HiddenDuration,
    TimeSpan ObservedEndpointTime)
{
    public double HiddenDuty => ObservedEndpointTime <= TimeSpan.Zero
        ? 0
        : Math.Clamp(
            HiddenDuration.TotalSeconds / ObservedEndpointTime.TotalSeconds,
            0,
            1);
}

internal readonly record struct ViewportPresentationOutcomeMetrics(
    int StaleCount,
    int SupersededCount,
    int FaultedCount,
    int QuarantinedCount);

internal readonly record struct ViewportPresentationCandidateMetrics(
    int PreparedCandidateCount,
    int WastedCandidateCount,
    long ProducedCount,
    long WasteCount)
{
    public double CandidateWasteRatio => PreparedCandidateCount == 0
        ? 0
        : (double)WastedCandidateCount / PreparedCandidateCount;

    // ProducedCount/WasteCount retain their original API names for existing smoke gates. Their
    // units are candidate-rendered frames, not candidate objects or imported resources.
    public double WasteRatio => ProducedCount == 0
        ? 0
        : (double)WasteCount / ProducedCount;
}

internal readonly record struct ViewportPresentationResourceMetrics(
    long HighWaterCount,
    long CountAtCapture,
    long ReclaimedCount,
    int ObservedSnapshotCount)
{
    public bool EvidenceAvailable => ObservedSnapshotCount != 0;
}

internal readonly record struct ViewportPresentationPhysicalDisplayMetrics(
    int ObservedSampleCount,
    int MatchedRenderedCount,
    int MissingAfterRenderCount,
    int UniqueGenerationCount)
{
    public bool EvidenceAvailable => ObservedSampleCount != 0;

    public double RenderCoverage => MatchedRenderedCount + MissingAfterRenderCount == 0
        ? 0
        : (double)MatchedRenderedCount /
            (MatchedRenderedCount + MissingAfterRenderCount);
}

internal readonly record struct ViewportPresentationTransactionTelemetryMetrics(
    int Capacity,
    int RetainedEventCount,
    long TotalRecordedEventCount,
    long OverflowCount,
    long RejectedEventCount,
    TimeSpan ObservationDuration,
    int UniquePublishedGenerationCount,
    int UniqueRenderedGenerationCount,
    ViewportPresentationStageCountMetrics StageCounts,
    ViewportPresentationStageLatencyMetrics StageLatencies,
    ViewportPresentationVisibilityMetrics Visibility,
    ViewportPresentationOutcomeMetrics Outcomes,
    ViewportPresentationCandidateMetrics Candidates,
    ViewportPresentationResourceMetrics Resources,
    ViewportPresentationPhysicalDisplayMetrics PhysicalDisplay)
{
    public bool HasOverflowed => OverflowCount != 0;

    // A rendered generation is the portable success boundary. Physical display evidence is
    // optional because a compositor/presentation monitor is not available in every smoke run.
    public double UniqueGenerationRate => ObservationDuration <= TimeSpan.Zero
        ? 0
        : UniqueRenderedGenerationCount / ObservationDuration.TotalSeconds;

    public double UniquePublishedGenerationRate => ObservationDuration <= TimeSpan.Zero
        ? 0
        : UniquePublishedGenerationCount / ObservationDuration.TotalSeconds;
}

internal sealed class ViewportPresentationTransactionTelemetry
{
    internal const int DefaultCapacity = 4096;

    private readonly object gate_ = new();
    private readonly RecordedEvent[] events_;
    private readonly long timestampFrequency_;
    private int count_;
    private int nextIndex_;
    private long nextSequence_;
    private long totalRecorded_;
    private long overflowCount_;
    private long rejectedCount_;

    public ViewportPresentationTransactionTelemetry(int capacity = DefaultCapacity)
        : this(capacity, Stopwatch.Frequency)
    {
    }

    internal ViewportPresentationTransactionTelemetry(int capacity, long timestampFrequency)
    {
        if (capacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }
        if (timestampFrequency <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(timestampFrequency));
        }

        events_ = new RecordedEvent[capacity];
        timestampFrequency_ = timestampFrequency;
    }

    public int Capacity => events_.Length;

    public IReadOnlyList<ViewportPresentationTelemetryEvent> CaptureEvents()
    {
        lock (gate_)
        {
            var recorded = SnapshotLocked();
            var result = new ViewportPresentationTelemetryEvent[recorded.Length];
            for (var index = 0; index < recorded.Length; index++)
            {
                result[index] = recorded[index].Event;
            }
            return result;
        }
    }

    public ViewportPresentationTelemetryRecordResult TryRecord(
        ViewportPresentationTelemetryEvent telemetryEvent)
    {
        lock (gate_)
        {
            if (!telemetryEvent.IsValid)
            {
                rejectedCount_ = checked(rejectedCount_ + 1);
                return ViewportPresentationTelemetryRecordResult.RejectedInvalidEvent;
            }

            var sequence = checked(++nextSequence_);
            events_[nextIndex_] = new RecordedEvent(sequence, telemetryEvent);
            nextIndex_ = (nextIndex_ + 1) % events_.Length;
            totalRecorded_ = checked(totalRecorded_ + 1);
            if (count_ == events_.Length)
            {
                overflowCount_ = checked(overflowCount_ + 1);
            }
            else
            {
                count_++;
            }
            return ViewportPresentationTelemetryRecordResult.Recorded;
        }
    }

    public void Record(ViewportPresentationTelemetryEvent telemetryEvent)
    {
        if (TryRecord(telemetryEvent) != ViewportPresentationTelemetryRecordResult.Recorded)
        {
            throw new ArgumentException(
                "Viewport presentation telemetry event is invalid.",
                nameof(telemetryEvent));
        }
    }

    public ViewportPresentationTransactionTelemetryMetrics Capture(long capturedTimestamp)
    {
        if (capturedTimestamp < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capturedTimestamp));
        }

        RecordedEvent[] retained;
        long totalRecorded;
        long overflowCount;
        long rejectedCount;
        lock (gate_)
        {
            retained = SnapshotLocked();
            totalRecorded = totalRecorded_;
            overflowCount = overflowCount_;
            rejectedCount = rejectedCount_;
        }

        Array.Sort(retained, static (left, right) =>
        {
            var timestampOrder = left.Event.Timestamp.CompareTo(right.Event.Timestamp);
            return timestampOrder != 0
                ? timestampOrder
                : left.Sequence.CompareTo(right.Sequence);
        });
        if (retained.Length != 0 && retained[^1].Event.Timestamp > capturedTimestamp)
        {
            throw new ArgumentOutOfRangeException(
                nameof(capturedTimestamp),
                "Capture timestamp must not precede a retained telemetry event.");
        }

        return BuildMetrics(
            retained,
            capturedTimestamp,
            totalRecorded,
            overflowCount,
            rejectedCount);
    }

    private RecordedEvent[] SnapshotLocked()
    {
        var result = new RecordedEvent[count_];
        var firstIndex = (nextIndex_ - count_ + events_.Length) % events_.Length;
        for (var offset = 0; offset < count_; offset++)
        {
            result[offset] = events_[(firstIndex + offset) % events_.Length];
        }
        return result;
    }

    private ViewportPresentationTransactionTelemetryMetrics BuildMetrics(
        IReadOnlyList<RecordedEvent> retained,
        long capturedTimestamp,
        long totalRecorded,
        long overflowCount,
        long rejectedCount)
    {
        var transactions = new Dictionary<TransactionKey, StageTimestamps>();
        var endpointVisibility = new Dictionary<EndpointScope, VisibilityState>();
        var publishedGenerations = new HashSet<GenerationKey>();
        var renderedGenerations = new HashSet<GenerationKey>();
        var physicalGenerations = new HashSet<GenerationKey>();
        var staleCount = 0;
        var supersededCount = 0;
        var faultedCount = 0;
        var quarantinedCount = 0;
        var candidateProduced = 0L;
        var candidateWaste = 0L;
        var preparedCandidates = 0;
        var wastedCandidates = 0;
        var resourceHighWater = 0L;
        var resourceAtCapture = 0L;
        var reclaimedResources = 0L;
        var resourceSnapshots = 0;

        foreach (var recorded in retained)
        {
            var telemetryEvent = recorded.Event;
            var identity = telemetryEvent.Identity;
            var transactionKey = new TransactionKey(identity);
            if (!transactions.TryGetValue(transactionKey, out var stages))
            {
                stages = new StageTimestamps();
                transactions.Add(transactionKey, stages);
            }

            var endpointScope = new EndpointScope(identity);
            if (!endpointVisibility.TryGetValue(endpointScope, out var visibility))
            {
                visibility = new VisibilityState(telemetryEvent.Timestamp);
                endpointVisibility.Add(endpointScope, visibility);
            }

            var generationKey = new GenerationKey(identity);
            switch (telemetryEvent.Kind)
            {
                case ViewportPresentationTelemetryEventKind.Proposed:
                    stages.Proposed ??= telemetryEvent.Timestamp;
                    break;
                case ViewportPresentationTelemetryEventKind.Prepared:
                    stages.Prepared ??= telemetryEvent.Timestamp;
                    break;
                case ViewportPresentationTelemetryEventKind.Published:
                    stages.Published ??= telemetryEvent.Timestamp;
                    publishedGenerations.Add(generationKey);
                    break;
                case ViewportPresentationTelemetryEventKind.Rendered:
                    stages.Rendered ??= telemetryEvent.Timestamp;
                    renderedGenerations.Add(generationKey);
                    break;
                case ViewportPresentationTelemetryEventKind.PhysicalDisplayed:
                    stages.PhysicalDisplayed ??= telemetryEvent.Timestamp;
                    physicalGenerations.Add(generationKey);
                    break;
                case ViewportPresentationTelemetryEventKind.HiddenStarted:
                    visibility.MarkHidden(telemetryEvent.Timestamp);
                    break;
                case ViewportPresentationTelemetryEventKind.HiddenEnded:
                    visibility.MarkVisible(telemetryEvent.Timestamp);
                    break;
                case ViewportPresentationTelemetryEventKind.Stale:
                    staleCount++;
                    break;
                case ViewportPresentationTelemetryEventKind.Superseded:
                    supersededCount++;
                    break;
                case ViewportPresentationTelemetryEventKind.Faulted:
                    faultedCount++;
                    break;
                case ViewportPresentationTelemetryEventKind.Quarantined:
                    quarantinedCount++;
                    break;
                case ViewportPresentationTelemetryEventKind.CandidateProduced:
                    preparedCandidates++;
                    candidateProduced = SaturatingAdd(candidateProduced, telemetryEvent.Amount);
                    break;
                case ViewportPresentationTelemetryEventKind.CandidateWasted:
                    wastedCandidates++;
                    candidateWaste = SaturatingAdd(candidateWaste, telemetryEvent.Amount);
                    break;
                case ViewportPresentationTelemetryEventKind.ResourceSnapshot:
                    resourceSnapshots++;
                    resourceAtCapture = telemetryEvent.Amount;
                    resourceHighWater = Math.Max(resourceHighWater, telemetryEvent.Amount);
                    break;
                case ViewportPresentationTelemetryEventKind.ResourceReclaimed:
                    reclaimedResources = SaturatingAdd(
                        reclaimedResources,
                        telemetryEvent.Amount);
                    break;
                default:
                    throw new InvalidOperationException(
                        $"Unknown viewport presentation telemetry event {telemetryEvent.Kind}.");
            }
        }

        var proposedToPrepared = new List<long>();
        var preparedToPublished = new List<long>();
        var publishedToRendered = new List<long>();
        var renderedToPhysical = new List<long>();
        var proposedToRendered = new List<long>();
        var proposedToPhysical = new List<long>();
        var physicalSamples = 0;
        var matchedPhysicalSamples = 0;
        var missingPhysicalSamples = 0;
        foreach (var stages in transactions.Values)
        {
            AddLatency(proposedToPrepared, stages.Proposed, stages.Prepared);
            AddLatency(preparedToPublished, stages.Prepared, stages.Published);
            AddLatency(publishedToRendered, stages.Published, stages.Rendered);
            AddLatency(renderedToPhysical, stages.Rendered, stages.PhysicalDisplayed);
            AddLatency(proposedToRendered, stages.Proposed, stages.Rendered);
            AddLatency(proposedToPhysical, stages.Proposed, stages.PhysicalDisplayed);
            if (stages.PhysicalDisplayed is not null)
            {
                physicalSamples++;
            }
            if (stages.Rendered is not null && stages.PhysicalDisplayed is not null)
            {
                matchedPhysicalSamples++;
            }
            else if (stages.Rendered is not null)
            {
                // Missing display evidence is informational, not a failed transaction.
                missingPhysicalSamples++;
            }
        }

        var hiddenTicks = 0d;
        var observedEndpointTicks = 0d;
        foreach (var visibility in endpointVisibility.Values)
        {
            hiddenTicks += visibility.HiddenTicksAt(capturedTimestamp);
            observedEndpointTicks += capturedTimestamp - visibility.FirstTimestamp;
        }

        var observationTicks = retained.Count == 0
            ? 0
            : capturedTimestamp - retained[0].Event.Timestamp;
        return new ViewportPresentationTransactionTelemetryMetrics(
            events_.Length,
            retained.Count,
            totalRecorded,
            overflowCount,
            rejectedCount,
            ToTimeSpan(observationTicks),
            publishedGenerations.Count,
            renderedGenerations.Count,
            new ViewportPresentationStageCountMetrics(
                transactions.Values.Count(static stages => stages.Proposed is not null),
                transactions.Values.Count(static stages => stages.Prepared is not null),
                transactions.Values.Count(static stages => stages.Published is not null),
                transactions.Values.Count(static stages => stages.Rendered is not null),
                transactions.Values.Count(static stages => stages.PhysicalDisplayed is not null)),
            new ViewportPresentationStageLatencyMetrics(
                Distribution(proposedToPrepared),
                Distribution(preparedToPublished),
                Distribution(publishedToRendered),
                Distribution(renderedToPhysical),
                Distribution(proposedToRendered),
                Distribution(proposedToPhysical)),
            new ViewportPresentationVisibilityMetrics(
                ToTimeSpan(hiddenTicks),
                ToTimeSpan(observedEndpointTicks)),
            new ViewportPresentationOutcomeMetrics(
                staleCount,
                supersededCount,
                faultedCount,
                quarantinedCount),
            new ViewportPresentationCandidateMetrics(
                preparedCandidates,
                wastedCandidates,
                candidateProduced,
                candidateWaste),
            new ViewportPresentationResourceMetrics(
                resourceHighWater,
                resourceAtCapture,
                reclaimedResources,
                resourceSnapshots),
            new ViewportPresentationPhysicalDisplayMetrics(
                physicalSamples,
                matchedPhysicalSamples,
                physicalSamples == 0 ? 0 : missingPhysicalSamples,
                physicalGenerations.Count));
    }

    private ViewportPresentationLatencyDistribution Distribution(List<long> values)
    {
        if (values.Count == 0)
        {
            return default;
        }

        values.Sort();
        return new ViewportPresentationLatencyDistribution(
            values.Count,
            ToTimeSpan(Percentile(values, 0.50)),
            ToTimeSpan(Percentile(values, 0.95)),
            ToTimeSpan(values[^1]));
    }

    private TimeSpan ToTimeSpan(long timestampTicks) => ToTimeSpan((double)timestampTicks);

    private TimeSpan ToTimeSpan(double timestampTicks)
    {
        if (timestampTicks <= 0)
        {
            return TimeSpan.Zero;
        }

        var timeSpanTicks = timestampTicks * TimeSpan.TicksPerSecond / timestampFrequency_;
        return timeSpanTicks >= TimeSpan.MaxValue.Ticks
            ? TimeSpan.MaxValue
            : TimeSpan.FromTicks((long)Math.Round(timeSpanTicks));
    }

    private static long Percentile(IReadOnlyList<long> sortedValues, double percentile)
    {
        var index = Math.Clamp(
            (int)Math.Ceiling(sortedValues.Count * percentile) - 1,
            0,
            sortedValues.Count - 1);
        return sortedValues[index];
    }

    private static void AddLatency(List<long> values, long? start, long? end)
    {
        if (start is { } startTimestamp &&
            end is { } endTimestamp &&
            endTimestamp >= startTimestamp)
        {
            values.Add(endTimestamp - startTimestamp);
        }
    }

    private static long SaturatingAdd(long left, long right) =>
        left > long.MaxValue - right ? long.MaxValue : left + right;

    private readonly record struct RecordedEvent(
        long Sequence,
        ViewportPresentationTelemetryEvent Event);

    private readonly record struct TransactionKey(
        ViewportPresentationEndpointId EndpointId,
        ViewportSessionId SessionId,
        ulong Epoch,
        ViewportPresentationTransactionId TransactionId,
        ulong Generation,
        ViewportExtent Extent)
    {
        public TransactionKey(ViewportPresentationTelemetryIdentity identity)
            : this(
                identity.EndpointId,
                identity.SessionId,
                identity.Epoch,
                identity.TransactionId,
                identity.Generation,
                identity.Extent)
        {
        }
    }

    private readonly record struct GenerationKey(
        ViewportPresentationEndpointId EndpointId,
        ViewportSessionId SessionId,
        ulong Epoch,
        ulong Generation)
    {
        public GenerationKey(ViewportPresentationTelemetryIdentity identity)
            : this(
                identity.EndpointId,
                identity.SessionId,
                identity.Epoch,
                identity.Generation)
        {
        }
    }

    private readonly record struct EndpointScope(
        ViewportPresentationEndpointId EndpointId,
        ViewportSessionId SessionId,
        ulong Epoch)
    {
        public EndpointScope(ViewportPresentationTelemetryIdentity identity)
            : this(identity.EndpointId, identity.SessionId, identity.Epoch)
        {
        }
    }

    private sealed class StageTimestamps
    {
        public long? Proposed { get; set; }

        public long? Prepared { get; set; }

        public long? Published { get; set; }

        public long? Rendered { get; set; }

        public long? PhysicalDisplayed { get; set; }
    }

    private sealed class VisibilityState
    {
        private long hiddenStartedTimestamp_;
        private long hiddenTicks_;
        private bool isHidden_;

        public VisibilityState(long firstTimestamp)
        {
            FirstTimestamp = firstTimestamp;
        }

        public long FirstTimestamp { get; }

        public void MarkHidden(long timestamp)
        {
            if (isHidden_)
            {
                return;
            }

            isHidden_ = true;
            hiddenStartedTimestamp_ = timestamp;
        }

        public void MarkVisible(long timestamp)
        {
            if (!isHidden_)
            {
                return;
            }

            hiddenTicks_ = SaturatingAdd(
                hiddenTicks_,
                Math.Max(0, timestamp - hiddenStartedTimestamp_));
            isHidden_ = false;
        }

        public double HiddenTicksAt(long capturedTimestamp)
        {
            return hiddenTicks_ + (isHidden_
                ? Math.Max(0, capturedTimestamp - hiddenStartedTimestamp_)
                : 0);
        }
    }
}

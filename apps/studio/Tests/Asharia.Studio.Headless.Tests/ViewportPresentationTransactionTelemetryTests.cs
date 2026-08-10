using System;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationTransactionTelemetryTests
{
    private const long TimestampFrequency = 1000;
    private static readonly ViewportSessionId SessionId = new(
        Guid.Parse("77c48134-faca-4caf-a525-2c46412646a0"));

    [Fact]
    public void Capture_reports_unique_rendered_rate_and_each_stage_distribution()
    {
        var telemetry = CreateTelemetry();
        var first = Identity(transaction: 1, generation: 10);
        var second = Identity(transaction: 2, generation: 11);
        RecordStages(telemetry, first, 0, 10, 20, 30, physicalDisplayed: 45);
        RecordStages(telemetry, second, 100, 120, 150, 180);

        var metrics = telemetry.Capture(capturedTimestamp: 1000);

        Assert.Equal(2, metrics.UniquePublishedGenerationCount);
        Assert.Equal(2, metrics.UniqueRenderedGenerationCount);
        Assert.Equal(2, metrics.StageCounts.Proposed);
        Assert.Equal(2, metrics.StageCounts.Prepared);
        Assert.Equal(2, metrics.StageCounts.Published);
        Assert.Equal(2, metrics.StageCounts.Rendered);
        Assert.Equal(1, metrics.StageCounts.PhysicalDisplayed);
        Assert.Equal(2, metrics.UniqueGenerationRate, precision: 6);
        Assert.Equal(2, metrics.UniquePublishedGenerationRate, precision: 6);
        AssertDistribution(
            metrics.StageLatencies.ProposedToPrepared,
            sampleCount: 2,
            p50Milliseconds: 10,
            p95Milliseconds: 20,
            maximumMilliseconds: 20);
        AssertDistribution(
            metrics.StageLatencies.PreparedToPublished,
            sampleCount: 2,
            p50Milliseconds: 10,
            p95Milliseconds: 30,
            maximumMilliseconds: 30);
        AssertDistribution(
            metrics.StageLatencies.PublishedToRendered,
            sampleCount: 2,
            p50Milliseconds: 10,
            p95Milliseconds: 30,
            maximumMilliseconds: 30);
        AssertDistribution(
            metrics.StageLatencies.ProposedToRendered,
            sampleCount: 2,
            p50Milliseconds: 30,
            p95Milliseconds: 80,
            maximumMilliseconds: 80);
        AssertDistribution(
            metrics.StageLatencies.RenderedToPhysicalDisplayed,
            sampleCount: 1,
            p50Milliseconds: 15,
            p95Milliseconds: 15,
            maximumMilliseconds: 15);
        AssertDistribution(
            metrics.StageLatencies.ProposedToPhysicalDisplayed,
            sampleCount: 1,
            p50Milliseconds: 45,
            p95Milliseconds: 45,
            maximumMilliseconds: 45);
    }

    [Fact]
    public void Physical_display_evidence_is_optional_and_missing_samples_are_informational()
    {
        var telemetry = CreateTelemetry();
        var displayed = Identity(transaction: 1, generation: 1);
        var unobserved = Identity(transaction: 2, generation: 2);
        RecordStages(telemetry, displayed, 0, 10, 20, 30, physicalDisplayed: 40);
        RecordStages(telemetry, unobserved, 50, 60, 70, 80);

        var metrics = telemetry.Capture(capturedTimestamp: 100);

        Assert.True(metrics.PhysicalDisplay.EvidenceAvailable);
        Assert.Equal(1, metrics.PhysicalDisplay.ObservedSampleCount);
        Assert.Equal(1, metrics.PhysicalDisplay.MatchedRenderedCount);
        Assert.Equal(1, metrics.PhysicalDisplay.MissingAfterRenderCount);
        Assert.Equal(1, metrics.PhysicalDisplay.UniqueGenerationCount);
        Assert.Equal(0.5, metrics.PhysicalDisplay.RenderCoverage, precision: 6);
        Assert.Equal(0, metrics.Outcomes.FaultedCount);

        var withoutMonitor = CreateTelemetry();
        RecordStages(
            withoutMonitor,
            Identity(transaction: 3, generation: 3),
            proposed: 0,
            prepared: 10,
            published: 20,
            rendered: 30);

        var monitorlessMetrics = withoutMonitor.Capture(capturedTimestamp: 100);

        Assert.False(monitorlessMetrics.PhysicalDisplay.EvidenceAvailable);
        Assert.Equal(0, monitorlessMetrics.PhysicalDisplay.MissingAfterRenderCount);
        Assert.Equal(0, monitorlessMetrics.StageLatencies.RenderedToPhysicalDisplayed.SampleCount);
        Assert.Equal(10, monitorlessMetrics.UniqueGenerationRate, precision: 6);
        Assert.False(monitorlessMetrics.Resources.EvidenceAvailable);
        Assert.Equal(0, monitorlessMetrics.Resources.ObservedSnapshotCount);
    }

    [Fact]
    public void Unique_generation_identity_includes_endpoint_session_and_epoch()
    {
        var telemetry = CreateTelemetry();
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            timestamp: 0,
            Identity(transaction: 1, generation: 7)));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            timestamp: 10,
            Identity(transaction: 2, generation: 7)));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            timestamp: 20,
            Identity(transaction: 3, generation: 7, endpoint: "game")));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            timestamp: 30,
            Identity(transaction: 4, generation: 7, epoch: 2)));

        var metrics = telemetry.Capture(capturedTimestamp: 1000);

        Assert.Equal(3, metrics.UniqueRenderedGenerationCount);
        Assert.Equal(3, metrics.UniqueGenerationRate, precision: 6);
    }

    [Fact]
    public void Hidden_duty_uses_observed_time_for_each_endpoint_scope()
    {
        var telemetry = CreateTelemetry();
        var scene = Identity(transaction: 1, generation: 1, endpoint: "scene");
        var game = Identity(transaction: 2, generation: 1, endpoint: "game");
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Proposed, 0, scene));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenStarted, 100, scene));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenStarted, 150, scene));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenEnded, 300, scene));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenStarted, 800, scene));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Proposed, 200, game));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenStarted, 400, game));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenEnded, 600, game));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.HiddenEnded, 650, game));

        var metrics = telemetry.Capture(capturedTimestamp: 1000);

        Assert.Equal(TimeSpan.FromMilliseconds(600), metrics.Visibility.HiddenDuration);
        Assert.Equal(TimeSpan.FromMilliseconds(1800), metrics.Visibility.ObservedEndpointTime);
        Assert.Equal(1d / 3d, metrics.Visibility.HiddenDuty, precision: 6);
    }

    [Fact]
    public void Capture_reports_outcomes_candidate_waste_and_resource_reclaim()
    {
        var telemetry = CreateTelemetry();
        var identity = Identity(transaction: 1, generation: 1);
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Proposed, 0, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Stale, 1, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Superseded, 2, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Faulted, 3, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Quarantined, 4, identity));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.CandidateProduced,
            5,
            identity,
            amount: 6));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.CandidateWasted,
            6,
            identity,
            amount: 2));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.CandidateWasted,
            7,
            identity,
            amount: 1));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            8,
            identity,
            amount: 2));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            9,
            identity,
            amount: 4));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceReclaimed,
            10,
            identity,
            amount: 3));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            11,
            identity,
            amount: 1));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceReclaimed,
            12,
            identity,
            amount: 2));

        var metrics = telemetry.Capture(capturedTimestamp: 1000);

        Assert.Equal(1, metrics.Outcomes.StaleCount);
        Assert.Equal(1, metrics.Outcomes.SupersededCount);
        Assert.Equal(1, metrics.Outcomes.FaultedCount);
        Assert.Equal(1, metrics.Outcomes.QuarantinedCount);
        Assert.Equal(6, metrics.Candidates.ProducedCount);
        Assert.Equal(3, metrics.Candidates.WasteCount);
        Assert.Equal(0.5, metrics.Candidates.WasteRatio, precision: 6);
        Assert.Equal(1, metrics.Candidates.PreparedCandidateCount);
        Assert.Equal(2, metrics.Candidates.WastedCandidateCount);
        Assert.Equal(2, metrics.Candidates.CandidateWasteRatio, precision: 6);
        Assert.Equal(4, metrics.Resources.HighWaterCount);
        Assert.Equal(1, metrics.Resources.CountAtCapture);
        Assert.Equal(5, metrics.Resources.ReclaimedCount);
        Assert.True(metrics.Resources.EvidenceAvailable);
        Assert.Equal(3, metrics.Resources.ObservedSnapshotCount);
    }

    [Fact]
    public void Fixed_capacity_overwrites_oldest_events_and_reports_overflow()
    {
        var telemetry = new ViewportPresentationTransactionTelemetry(
            capacity: 3,
            timestampFrequency: TimestampFrequency);
        var identity = Identity(transaction: 1, generation: 1);
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Stale, 0, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Superseded, 1, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Faulted, 2, identity));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Quarantined, 3, identity));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.CandidateProduced,
            4,
            identity,
            amount: 2));

        var metrics = telemetry.Capture(capturedTimestamp: 10);

        Assert.Equal(3, metrics.Capacity);
        Assert.Equal(3, metrics.RetainedEventCount);
        Assert.Equal(5, metrics.TotalRecordedEventCount);
        Assert.Equal(2, metrics.OverflowCount);
        Assert.True(metrics.HasOverflowed);
        Assert.Equal(0, metrics.Outcomes.StaleCount);
        Assert.Equal(0, metrics.Outcomes.SupersededCount);
        Assert.Equal(1, metrics.Outcomes.FaultedCount);
        Assert.Equal(1, metrics.Outcomes.QuarantinedCount);
        Assert.Equal(2, metrics.Candidates.ProducedCount);
    }

    [Fact]
    public void Invalid_events_are_rejected_without_consuming_ring_capacity()
    {
        var telemetry = new ViewportPresentationTransactionTelemetry(
            capacity: 2,
            timestampFrequency: TimestampFrequency);
        var identity = Identity(transaction: 1, generation: 1);

        Assert.Equal(
            ViewportPresentationTelemetryRecordResult.RejectedInvalidEvent,
            telemetry.TryRecord(Event(
                ViewportPresentationTelemetryEventKind.CandidateProduced,
                timestamp: 0,
                identity,
                amount: 0)));
        Assert.Equal(
            ViewportPresentationTelemetryRecordResult.RejectedInvalidEvent,
            telemetry.TryRecord(Event(
                ViewportPresentationTelemetryEventKind.Rendered,
                timestamp: 1,
                identity,
                amount: 1)));
        Assert.Equal(
            ViewportPresentationTelemetryRecordResult.RejectedInvalidEvent,
            telemetry.TryRecord(new ViewportPresentationTelemetryEvent(
                ViewportPresentationTelemetryEventKind.Proposed,
                Timestamp: 2,
                Identity: default)));
        telemetry.Record(Event(ViewportPresentationTelemetryEventKind.Proposed, 3, identity));

        var metrics = telemetry.Capture(capturedTimestamp: 10);

        Assert.Equal(1, metrics.RetainedEventCount);
        Assert.Equal(1, metrics.TotalRecordedEventCount);
        Assert.Equal(3, metrics.RejectedEventCount);
        Assert.Equal(0, metrics.OverflowCount);
        Assert.Throws<ArgumentException>(() => telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            timestamp: 11,
            identity,
            amount: -1)));
    }

    [Fact]
    public void Percentiles_use_nearest_rank_and_recording_may_arrive_out_of_order()
    {
        var telemetry = new ViewportPresentationTransactionTelemetry(
            capacity: 64,
            timestampFrequency: TimestampFrequency);
        for (var delay = 20; delay >= 1; delay--)
        {
            var identity = Identity(
                transaction: checked((ulong)delay),
                generation: checked((ulong)delay));
            var proposed = delay * 100L;
            telemetry.Record(Event(
                ViewportPresentationTelemetryEventKind.Prepared,
                proposed + delay,
                identity));
            telemetry.Record(Event(
                ViewportPresentationTelemetryEventKind.Proposed,
                proposed,
                identity));
        }

        var metrics = telemetry.Capture(capturedTimestamp: 3000);

        AssertDistribution(
            metrics.StageLatencies.ProposedToPrepared,
            sampleCount: 20,
            p50Milliseconds: 10,
            p95Milliseconds: 19,
            maximumMilliseconds: 20);
    }

    private static ViewportPresentationTransactionTelemetry CreateTelemetry() =>
        new(capacity: 128, timestampFrequency: TimestampFrequency);

    private static ViewportPresentationTelemetryIdentity Identity(
        ulong transaction,
        ulong generation,
        string endpoint = "scene",
        ulong epoch = 1) =>
        new(
            new ViewportPresentationEndpointId(endpoint),
            SessionId,
            epoch,
            new ViewportPresentationTransactionId(transaction),
            generation,
            new ViewportExtent(width: 640, height: 360));

    private static ViewportPresentationTelemetryEvent Event(
        ViewportPresentationTelemetryEventKind kind,
        long timestamp,
        ViewportPresentationTelemetryIdentity identity,
        long amount = 0) =>
        new(kind, timestamp, identity, amount);

    private static void RecordStages(
        ViewportPresentationTransactionTelemetry telemetry,
        ViewportPresentationTelemetryIdentity identity,
        long proposed,
        long prepared,
        long published,
        long rendered,
        long? physicalDisplayed = null)
    {
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Proposed,
            proposed,
            identity));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Prepared,
            prepared,
            identity));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Published,
            published,
            identity));
        telemetry.Record(Event(
            ViewportPresentationTelemetryEventKind.Rendered,
            rendered,
            identity));
        if (physicalDisplayed is { } timestamp)
        {
            telemetry.Record(Event(
                ViewportPresentationTelemetryEventKind.PhysicalDisplayed,
                timestamp,
                identity));
        }
    }

    private static void AssertDistribution(
        ViewportPresentationLatencyDistribution distribution,
        int sampleCount,
        double p50Milliseconds,
        double p95Milliseconds,
        double maximumMilliseconds)
    {
        Assert.Equal(sampleCount, distribution.SampleCount);
        Assert.Equal(p50Milliseconds, distribution.P50.TotalMilliseconds, precision: 6);
        Assert.Equal(p95Milliseconds, distribution.P95.TotalMilliseconds, precision: 6);
        Assert.Equal(
            maximumMilliseconds,
            distribution.Maximum.TotalMilliseconds,
            precision: 6);
    }
}

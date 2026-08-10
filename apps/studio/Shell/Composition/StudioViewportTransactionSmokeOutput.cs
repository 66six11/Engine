using System;
using System.Text.Json;
using Asharia.Studio.Presentation.Avalonia.Viewports;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionSmokeOutput
{
    public static void WriteSummary(
        string scenario,
        ViewportPresentationTransactionTelemetryMetrics metrics,
        double requestedHiddenDuty)
    {
        var stages = metrics.StageLatencies;
        Console.Out.WriteLine(
            "viewport-transaction-metrics " + JsonSerializer.Serialize(new
            {
                scenario,
                uniquePublished = metrics.UniquePublishedGenerationCount,
                uniqueRendered = metrics.UniqueRenderedGenerationCount,
                uniqueRenderedPerSecond = metrics.UniqueGenerationRate,
                stages = new
                {
                    proposed = metrics.StageCounts.Proposed,
                    prepared = metrics.StageCounts.Prepared,
                    published = metrics.StageCounts.Published,
                    rendered = metrics.StageCounts.Rendered,
                    physicalDisplayed = metrics.StageCounts.PhysicalDisplayed,
                },
                proposedToPrepared = Distribution(stages.ProposedToPrepared),
                preparedToPublished = Distribution(stages.PreparedToPublished),
                publishedToRendered = Distribution(stages.PublishedToRendered),
                proposedToRendered = Distribution(stages.ProposedToRendered),
                renderedToPhysicalDisplayed = Distribution(
                    stages.RenderedToPhysicalDisplayed),
                proposedToPhysicalDisplayed = Distribution(
                    stages.ProposedToPhysicalDisplayed),
                requestedHiddenDuty,
                participantOutcomeEvents = new
                {
                    stale = metrics.Outcomes.StaleCount,
                    superseded = metrics.Outcomes.SupersededCount,
                    faulted = metrics.Outcomes.FaultedCount,
                    quarantined = metrics.Outcomes.QuarantinedCount,
                },
                candidates = new
                {
                    prepared = metrics.Candidates.PreparedCandidateCount,
                    wasted = metrics.Candidates.WastedCandidateCount,
                    candidateWasteRatio = metrics.Candidates.CandidateWasteRatio,
                    renderedFrames = metrics.Candidates.ProducedCount,
                    wastedRenderedFrames = metrics.Candidates.WasteCount,
                    renderedFrameWasteRatio = metrics.Candidates.WasteRatio,
                },
                resources = new
                {
                    evidenceAvailable = metrics.Resources.EvidenceAvailable,
                    snapshots = metrics.Resources.ObservedSnapshotCount,
                    highWater = metrics.Resources.EvidenceAvailable
                        ? metrics.Resources.HighWaterCount
                        : (long?)null,
                    current = metrics.Resources.EvidenceAvailable
                        ? metrics.Resources.CountAtCapture
                        : (long?)null,
                    reclaimed = metrics.Resources.EvidenceAvailable
                        ? metrics.Resources.ReclaimedCount
                        : (long?)null,
                },
                physicalDisplay = new
                {
                    evidenceAvailable = metrics.PhysicalDisplay.EvidenceAvailable,
                    observed = metrics.PhysicalDisplay.ObservedSampleCount,
                    matchedRendered = metrics.PhysicalDisplay.MatchedRenderedCount,
                    missingAfterRender = metrics.PhysicalDisplay.EvidenceAvailable
                        ? metrics.PhysicalDisplay.MissingAfterRenderCount
                        : (int?)null,
                },
                retainedEvents = metrics.RetainedEventCount,
                overflow = metrics.OverflowCount,
                rejectedEvents = metrics.RejectedEventCount,
            }));
    }

    public static void WriteEvents(
        string scenario,
        ViewportPresentationTransactionTelemetry telemetry)
    {
        foreach (var telemetryEvent in telemetry.CaptureEvents())
        {
            Console.Out.WriteLine(
                "viewport-transaction-event " + JsonSerializer.Serialize(new
                {
                    scenario,
                    kind = telemetryEvent.Kind.ToString(),
                    qpc = telemetryEvent.Timestamp,
                    endpoint = telemetryEvent.EndpointId.Value,
                    session = telemetryEvent.SessionId.Value,
                    epoch = telemetryEvent.Epoch,
                    transaction = telemetryEvent.TransactionId.Value,
                    generation = telemetryEvent.Generation,
                    extent = new
                    {
                        width = telemetryEvent.Extent.Width,
                        height = telemetryEvent.Extent.Height,
                    },
                    amount = telemetryEvent.Amount,
                }));
        }
    }

    private static object Distribution(ViewportPresentationLatencyDistribution distribution) =>
        new
        {
            count = distribution.SampleCount,
            p50Ms = distribution.P50.TotalMilliseconds,
            p95Ms = distribution.P95.TotalMilliseconds,
            maxMs = distribution.Maximum.TotalMilliseconds,
        };
}

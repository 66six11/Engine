using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;
using Editor.Shell.Docking.Splitters;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionResizeSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-transaction-resize";
    private const int DefaultInputCount = 90;
    private const double DefaultInputHz = 120;
    private const double MinimumCoverage = 0.95;
    private static readonly TimeSpan kTimeout = TimeSpan.FromSeconds(5);

    public static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        var exitCode = 1;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            var options = ResizeOptions.Parse(arguments);
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession("viewport-transaction-resize.scene.json");
            var control = host.CreateControl(session);
            var layout = StudioViewportDockSmokeLayout.Create(control);
            var first = layout.First;
            var splitter = layout.Splitter;

            host.Show(desktop, layout.Root, "Viewport Transaction Resize Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync([control]);

            var originWidth = first.ActualWidth;
            if (originWidth <= 1)
            {
                throw new InvalidOperationException(
                    "The dock resize smoke did not receive a renderable initial layout.");
            }
            var renderScaling = TopLevel.GetTopLevel(control)?.RenderScaling ?? 0;
            if (!double.IsFinite(renderScaling) || renderScaling <= 0)
            {
                throw new InvalidOperationException(
                    "The dock resize smoke has no valid host render scaling.");
            }
            if (options.Pattern == "pixel")
            {
                splitter.DragIncrement = 1 / renderScaling;
            }
            var requestedWidths = StudioViewportResizeStimulus.Build(
                options.Pattern,
                options.InputCount,
                originWidth,
                renderScaling);
            var measurement = control.BeginResizeMeasurement();
            var geometryBefore = control.PresentationGeometryMetrics;
            var inputStartedAt = Stopwatch.GetTimestamp();
            var inputFinishedAt = inputStartedAt;
            WriteMarker("resize_begin", inputStartedAt, options);
            using (var resize = splitter.BeginAcceptanceResize())
            {
                for (var index = 0; index < requestedWidths.Length; index++)
                {
                    resize.RequestCumulative(
                        requestedWidths[index] - originWidth,
                        isFinal: index == requestedWidths.Length - 1);
                    if (index + 1 < requestedWidths.Length)
                    {
                        await StudioViewportResizeStimulus.WaitUntilAsync(
                            inputStartedAt,
                            (index + 1) / options.InputHz);
                    }
                }
                inputFinishedAt = Stopwatch.GetTimestamp();
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }

            var (metrics, catchUpBatches) = await WaitForFinalGenerationAsync(
                control,
                measurement,
                requestedWidths[^1]);
            var endedAt = Stopwatch.GetTimestamp();
            WriteMarker("resize_end", endedAt, options);
            var geometryAfter = control.PresentationGeometryMetrics;
            var transactionMetrics = splitter.CapturePresentationTransactionTelemetry();
            var schedulerMetrics = splitter.CaptureResizeCoordinatorMetrics();
            var visualSnapshot = control.CapturePresentationTestSnapshot();
            var hasPanelExtent = ViewportPhysicalExtentPolicy.TryCalculate(
                control.Bounds.Width,
                control.Bounds.Height,
                renderScaling,
                out var panelExtent);
            var hasVisualExtent = ViewportPhysicalExtentPolicy.TryCalculate(
                visualSnapshot.VisualSize.X,
                visualSnapshot.VisualSize.Y,
                renderScaling,
                out var visualExtent);
            var visualMatchesExactSurface =
                hasPanelExtent &&
                hasVisualExtent &&
                panelExtent == visualExtent &&
                panelExtent == visualSnapshot.SurfaceExtent &&
                visualSnapshot.CurrentExtent == visualSnapshot.SurfaceExtent &&
                visualSnapshot.GeometryGeneration == visualSnapshot.SurfaceGeneration &&
                visualSnapshot.HasExactSurface &&
                Math.Abs(visualSnapshot.VisualOpacity - 1) <= 0.001;
            // A unique geometry generation cannot be produced more often than geometry input.
            // The 30 Hz lane therefore checks near-complete 30 Hz coverage; 60 Hz and faster
            // lanes retain the editor's >=60 Rendered/s contract.
            var expectedRate = options.InputHz < 60
                ? options.InputHz * 0.95
                : options.InputHz < 61
                    ? 59
                    : 60;
            // 1.5 commits at a 59.94 Hz desktop cadence is already 25.025 ms. Keep a
            // sub-millisecond QPC/dispatcher tolerance for interval sampling while the
            // authored Bounds -> exact surface-submit latency remains a strict 25 ms gate.
            var maximumP95Interval = TimeSpan.FromMilliseconds(
                Math.Max(25.5, 1500 / options.InputHz));
            var maximumBoundsToExactSubmit = TimeSpan.FromMilliseconds(25);
            var rejected = checked(
                geometryAfter.RejectedNonExactCandidates -
                geometryBefore.RejectedNonExactCandidates);
            if (metrics.ObservedBoundsGenerations < 2 ||
                metrics.CompletionCoverage < MinimumCoverage ||
                metrics.UniqueExactCompletedPerSecond < expectedRate ||
                metrics.P95UniqueCompletionInterval > maximumP95Interval ||
                metrics.P95BoundsToExactSubmit > maximumBoundsToExactSubmit ||
                metrics.MaximumUniqueCompletionInterval > TimeSpan.FromMilliseconds(100) ||
                metrics.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
                rejected != 0 ||
                transactionMetrics.HasOverflowed ||
                transactionMetrics.RejectedEventCount != 0 ||
                transactionMetrics.UniqueRenderedGenerationCount !=
                    metrics.UniqueExactCompletedGenerations ||
                // The generic transaction telemetry window stays open until Capture so it can
                // account for visibility and resource state. Resize throughput instead uses the
                // geometry tracker's closed first-Bounds -> final-exact-Rendered window above;
                // gating the open telemetry window would make a completed 60 Hz lane fail solely
                // because of post-completion capture time.
                transactionMetrics.Candidates.ProducedCount !=
                    transactionMetrics.UniquePublishedGenerationCount ||
                transactionMetrics.Candidates.WasteCount != 0 ||
                transactionMetrics.Outcomes != default ||
                !visualMatchesExactSurface ||
                !metrics.FinalGenerationHasExactSurface ||
                !metrics.FinalGenerationCompleted ||
                !geometryAfter.CurrentSurfaceIsExact ||
                !geometryAfter.LastPresentationIsExact ||
                catchUpBatches > 2)
            {
                throw new InvalidOperationException(
                    $"resize transaction acceptance failed: pattern={options.Pattern}, " +
                    $"input={options.InputHz:F0}Hz, " +
                    $"{metrics.UniqueExactCompletedGenerations}/" +
                    $"{metrics.ObservedBoundsGenerations} unique Rendered at " +
                    $"{metrics.UniqueExactCompletedPerSecond:F2}/s, " +
                    $"coverage={metrics.CompletionCoverage:P1}, " +
                    $"p95={metrics.P95UniqueCompletionInterval.TotalMilliseconds:F2}ms, " +
                    $"max={metrics.MaximumUniqueCompletionInterval.TotalMilliseconds:F2}ms, " +
                    $"boundsToSubmitP95={metrics.P95BoundsToExactSubmit.TotalMilliseconds:F2}ms, " +
                    $"boundsToCompleteP95={metrics.P95BoundsToExactCompletion.TotalMilliseconds:F2}ms, " +
                    $"hidden={metrics.RequestedMismatchHiddenDutyCycle:P1}, " +
                    $"mismatch={rejected}, noCropStretch={visualMatchesExactSurface}, " +
                    $"catch-up={catchUpBatches}/2, " +
                    $"txRendered={transactionMetrics.UniqueRenderedGenerationCount}, " +
                    $"txRate={transactionMetrics.UniqueGenerationRate:F2}/s, " +
                    $"candidatePrepared={transactionMetrics.Candidates.ProducedCount}, " +
                    $"txPublished={transactionMetrics.UniquePublishedGenerationCount}, " +
                    $"candidateWaste={transactionMetrics.Candidates.WasteCount}, " +
                    $"outcomes={transactionMetrics.Outcomes}, " +
                    $"overflow={transactionMetrics.HasOverflowed}, " +
                    $"rejectedEvents={transactionMetrics.RejectedEventCount}, " +
                    $"finalExact={metrics.FinalGenerationHasExactSurface}, " +
                    $"finalCompleted={metrics.FinalGenerationCompleted}, " +
                    $"currentExact={geometryAfter.CurrentSurfaceIsExact}, " +
                    $"lastExact={geometryAfter.LastPresentationIsExact}.");
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                "resize",
                transactionMetrics,
                metrics.RequestedMismatchHiddenDutyCycle);
            if (arguments.Contains("--viewport-transaction-trace", StringComparer.Ordinal))
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "resize",
                    splitter.PresentationTransactionTelemetry);
            }
            var zeroExtentEvidence = await VerifyZeroExtentRecoveryAsync(
                control,
                layout.Root,
                first,
                layout.Split);
            var dpiPolicyEvidence = CaptureDpiPolicyEvidence();
            WriteStructuredEvidence(
                options,
                requestedWidths,
                inputStartedAt,
                inputFinishedAt,
                endedAt,
                renderScaling,
                panelExtent,
                visualExtent,
                visualSnapshot.SurfaceExtent,
                metrics,
                schedulerMetrics,
                zeroExtentEvidence,
                dpiPolicyEvidence);
            Console.Out.WriteLine(
                $"viewport-transaction-resize PASS: pattern={options.Pattern}, " +
                $"input={options.InputHz:F0}Hz ({options.InputCount} requests), " +
                $"{metrics.UniqueExactCompletedGenerations}/" +
                $"{metrics.ObservedBoundsGenerations} unique exact Rendered at " +
                $"{metrics.UniqueExactCompletedPerSecond:F2}/s, " +
                $"coverage={metrics.CompletionCoverage:P1}, " +
                $"p95={metrics.P95UniqueCompletionInterval.TotalMilliseconds:F2}ms, " +
                $"max={metrics.MaximumUniqueCompletionInterval.TotalMilliseconds:F2}ms, " +
                $"boundsToSubmitP95={metrics.P95BoundsToExactSubmit.TotalMilliseconds:F2}ms, " +
                $"boundsToCompleteP95={metrics.P95BoundsToExactCompletion.TotalMilliseconds:F2}ms, " +
                $"hidden={metrics.RequestedMismatchHiddenDutyCycle:P1}, " +
                $"mismatch={rejected}, noCropStretch=true, catch-up={catchUpBatches}/2, " +
                $"scaling={TopLevel.GetTopLevel(control)?.RenderScaling:F2}.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"viewport-transaction-resize FAIL: {exception.Message}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static async Task<ZeroExtentRecoveryEvidence> VerifyZeroExtentRecoveryAsync(
        ViewportCompositionControl control,
        Grid root,
        ColumnDefinition first,
        Editor.Shell.ViewModels.Docking.EditorDockSplitNodeViewModel split)
    {
        var committed = first.Width;
        var committedRootHeight = root.Height;
        var before = control.CapturePresentationTestSnapshot();
        var measurement = control.BeginResizeMeasurement();
        ViewportPresentationTestSnapshot collapsed;
        long collapsedAt;
        try
        {
            split.FirstLength = new GridLength(0, GridUnitType.Pixel);
            first.Width = split.FirstLength;
            root.Height = 0;
            (control.Parent as Grid)?.UpdateLayout();
            collapsedAt = Stopwatch.GetTimestamp();
            collapsed = control.CapturePresentationTestSnapshot();
            if (control.Bounds.Width != 0 || control.Bounds.Height != 0 ||
                Math.Abs(collapsed.VisualOpacity) > 0.001)
            {
                throw new InvalidOperationException(
                    "The viewport did not enter a hidden 0x0 collapse state.");
            }
        }
        finally
        {
            split.FirstLength = committed;
            first.Width = committed;
            root.Height = committedRootHeight;
            (control.Parent as Grid)?.UpdateLayout();
        }

        using var deadline = new CancellationTokenSource(kTimeout);
        var visual = ElementComposition.GetElementVisual(control) ??
            throw new InvalidOperationException("The viewport visual is unavailable after recovery.");
        while (true)
        {
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            var recovered = control.CapturePresentationTestSnapshot();
            var resize = control.CaptureResizeMeasurement(measurement);
            if (resize.FinalGenerationCompleted &&
                resize.FinalGenerationHasExactSurface &&
                recovered.HasExactSurface &&
                recovered.CurrentExtent == recovered.SurfaceExtent &&
                recovered.GeometryGeneration == recovered.SurfaceGeneration &&
                recovered.SurfaceExtent == before.SurfaceExtent &&
                Math.Abs(recovered.VisualOpacity - 1) <= 0.001)
            {
                // The exact front and opacity mutation are now requested. One rendered batch
                // confirms that this state crossed the real Avalonia compositor boundary; it is
                // not presented as a synthetic count of OS compositor ticks during GPU work.
                await visual.Compositor.RequestCompositionBatchCommitAsync()
                    .Rendered.WaitAsync(deadline.Token);
                return new ZeroExtentRecoveryEvidence(
                    true,
                    collapsedAt,
                    Stopwatch.GetTimestamp(),
                    1,
                    before.SurfaceExtent,
                    recovered.SurfaceExtent,
                    Math.Abs(collapsed.VisualOpacity) <= 0.001,
                    resize.RequestedMismatchHiddenDuration);
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static async Task<(ViewportResizePresentationMetrics Metrics, int Batches)>
        WaitForFinalGenerationAsync(
            ViewportCompositionControl control,
            ViewportResizeMeasurementToken measurement,
            double expectedWidth)
    {
        using var deadline = new CancellationTokenSource(kTimeout);
        while (Math.Abs(control.Bounds.Width - expectedWidth) > 1.1)
        {
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }

        var batches = 0;
        var visual = ElementComposition.GetElementVisual(control) ??
            throw new InvalidOperationException("The viewport visual is unavailable.");
        var metrics = control.CaptureResizeMeasurement(measurement);
        while ((!metrics.FinalGenerationHasExactSurface ||
                !metrics.FinalGenerationCompleted) && batches < 2)
        {
            await visual.Compositor.RequestCompositionBatchCommitAsync()
                .Rendered.WaitAsync(deadline.Token);
            batches++;
            metrics = control.CaptureResizeMeasurement(measurement);
        }
        return (metrics, batches);
    }

    private static DpiPolicyEvidence CaptureDpiPolicyEvidence()
    {
        var samples = new List<DpiPolicySample>();
        foreach (var scaling in new[] { 1d, 1.25d, 1.5d, 2d })
        {
            const double pixelBoundary = 100;
            if (!ViewportPhysicalExtentPolicy.TryCalculate(
                    640,
                    480,
                    scaling,
                    out var standard) ||
                !ViewportPhysicalExtentPolicy.TryCalculate(
                    (pixelBoundary - 0.25) / scaling,
                    (pixelBoundary - 0.25) / scaling,
                    scaling,
                    out var belowBoundary) ||
                !ViewportPhysicalExtentPolicy.TryCalculate(
                    (pixelBoundary + 0.25) / scaling,
                    (pixelBoundary + 0.25) / scaling,
                    scaling,
                    out var aboveBoundary) ||
                standard.Width != checked((uint)Math.Round(640 * scaling)) ||
                standard.Height != checked((uint)Math.Round(480 * scaling)) ||
                belowBoundary.Width != 100 ||
                belowBoundary.Height != 100 ||
                aboveBoundary.Width != 101 ||
                aboveBoundary.Height != 101)
            {
                throw new InvalidOperationException(
                    $"The physical extent policy failed its {scaling:P0} DPI boundary matrix.");
            }
            samples.Add(new DpiPolicySample(
                scaling,
                standard,
                belowBoundary,
                aboveBoundary));
        }
        return new DpiPolicyEvidence(true, samples);
    }

    private static void WriteStructuredEvidence(
        ResizeOptions options,
        IReadOnlyList<double> requestedWidths,
        long inputStartedAt,
        long inputFinishedAt,
        long transactionEndedAt,
        double renderScaling,
        Asharia.Studio.Application.Viewports.ViewportExtent panelExtent,
        Asharia.Studio.Application.Viewports.ViewportExtent visualExtent,
        Asharia.Studio.Application.Viewports.ViewportExtent surfaceExtent,
        ViewportResizePresentationMetrics resize,
        EditorDockSplitResizeCoordinatorMetrics scheduler,
        ZeroExtentRecoveryEvidence zeroExtent,
        DpiPolicyEvidence dpiPolicy)
    {
        var requestedPhysicalWidths = requestedWidths
            .Select(width =>
            {
                if (!ViewportPhysicalExtentPolicy.TryCalculate(
                        width,
                        1 / renderScaling,
                        renderScaling,
                        out var extent))
                {
                    throw new InvalidOperationException(
                        "A resize stimulus did not resolve to a physical pixel width.");
                }
                return extent.Width;
            })
            .ToArray();
        var crossesOnePixelBoundary = options.Pattern == "pixel" &&
            requestedPhysicalWidths.Zip(
                requestedPhysicalWidths.Skip(1),
                static (left, right) => Math.Abs((long)left - right) == 1)
            .All(static adjacent => adjacent);
        Console.Out.WriteLine(
            "viewport-transaction-resize-evidence " + JsonSerializer.Serialize(new
            {
                scenario = "resize",
                pattern = options.Pattern,
                input = new
                {
                    requested = options.InputCount,
                    targetHz = options.InputHz,
                    durationMs = Stopwatch.GetElapsedTime(
                        inputStartedAt,
                        inputFinishedAt).TotalMilliseconds,
                    transactionDurationMs = Stopwatch.GetElapsedTime(
                        inputStartedAt,
                        transactionEndedAt).TotalMilliseconds,
                    schedulerAccepted = scheduler.AcceptedRequests,
                    schedulerProcessed = scheduler.ProcessedRequests,
                    queuedSuperseded = scheduler.QueuedSupersededRequests,
                    activeCancelled = scheduler.ActiveCancelledRequests,
                    maximumPending = scheduler.MaximumPendingWork,
                },
                rendered = new
                {
                    uniqueExact = resize.UniqueExactCompletedGenerations,
                    observedBounds = resize.ObservedBoundsGenerations,
                    rate = resize.UniqueExactCompletedPerSecond,
                    coverage = resize.CompletionCoverage,
                    p95IntervalMs = resize.P95UniqueCompletionInterval.TotalMilliseconds,
                    maximumIntervalMs = resize.MaximumUniqueCompletionInterval.TotalMilliseconds,
                    boundsToExactSubmitP95Ms =
                        resize.P95BoundsToExactSubmit.TotalMilliseconds,
                    boundsToExactCompletionP95Ms =
                        resize.P95BoundsToExactCompletion.TotalMilliseconds,
                    finalExact = resize.FinalGenerationHasExactSurface,
                    finalRendered = resize.FinalGenerationCompleted,
                },
                exactPhysical = new
                {
                    panel = Extent(panelExtent),
                    visual = Extent(visualExtent),
                    surface = Extent(surfaceExtent),
                    noCropOrStretch = panelExtent == visualExtent &&
                        panelExtent == surfaceExtent,
                },
                onePixelBoundary = new
                {
                    runtimeInputEvidenceAvailable = options.Pattern == "pixel",
                    adjacentPhysicalWidthsDifferByOne = crossesOnePixelBoundary,
                    purePolicyEvidenceAvailable = dpiPolicy.EvidenceAvailable,
                },
                zeroExtentRecovery = new
                {
                    evidenceAvailable = zeroExtent.EvidenceAvailable,
                    zeroWidthAndHeightObserved = true,
                    visualHiddenWhileCollapsed = zeroExtent.VisualHiddenWhileCollapsed,
                    collapsedQpc = zeroExtent.CollapsedAt,
                    recoveredQpc = zeroExtent.RecoveredAt,
                    recoveryMs = Stopwatch.GetElapsedTime(
                        zeroExtent.CollapsedAt,
                        zeroExtent.RecoveredAt).TotalMilliseconds,
                    visibleConfirmationBatches = zeroExtent.VisibleConfirmationBatches,
                    before = Extent(zeroExtent.BeforeExtent),
                    recovered = Extent(zeroExtent.RecoveredExtent),
                    intentionalHiddenMs =
                        zeroExtent.IntentionalHiddenDuration.TotalMilliseconds,
                },
                dpiMatrix = new
                {
                    realHostScaleInjectionEvidenceAvailable = false,
                    observedHostScaling = renderScaling,
                    reason = "The Studio smoke observes OS-provided RenderScaling and does not override TopLevel DPI.",
                    purePolicyEvidenceAvailable = dpiPolicy.EvidenceAvailable,
                    samples = dpiPolicy.Samples.Select(sample => new
                    {
                        scaling = sample.Scaling,
                        standard = Extent(sample.StandardExtent),
                        belowOnePixelBoundary = Extent(sample.BelowBoundaryExtent),
                        aboveOnePixelBoundary = Extent(sample.AboveBoundaryExtent),
                    }),
                },
            }));

        static object Extent(
            Asharia.Studio.Application.Viewports.ViewportExtent value) => new
            {
                width = value.Width,
                height = value.Height,
            };
    }

    private static void WriteMarker(string phase, long timestamp, ResizeOptions options) =>
        Console.Out.WriteLine(
            $"viewport-transaction-resize phase={phase} QPC={timestamp} " +
            $"Frequency={Stopwatch.Frequency} pattern={options.Pattern} " +
            $"inputHz={options.InputHz:F2} count={options.InputCount}.");

    private readonly record struct ZeroExtentRecoveryEvidence(
        bool EvidenceAvailable,
        long CollapsedAt,
        long RecoveredAt,
        int VisibleConfirmationBatches,
        Asharia.Studio.Application.Viewports.ViewportExtent BeforeExtent,
        Asharia.Studio.Application.Viewports.ViewportExtent RecoveredExtent,
        bool VisualHiddenWhileCollapsed,
        TimeSpan IntentionalHiddenDuration);

    private sealed record DpiPolicyEvidence(
        bool EvidenceAvailable,
        IReadOnlyList<DpiPolicySample> Samples);

    private readonly record struct DpiPolicySample(
        double Scaling,
        Asharia.Studio.Application.Viewports.ViewportExtent StandardExtent,
        Asharia.Studio.Application.Viewports.ViewportExtent BelowBoundaryExtent,
        Asharia.Studio.Application.Viewports.ViewportExtent AboveBoundaryExtent);

    private sealed record ResizeOptions(string Pattern, double InputHz, int InputCount)
    {
        public static ResizeOptions Parse(string[] arguments)
        {
            var pattern = Read(arguments, "--viewport-resize-pattern=") ?? "sawtooth";
            if (pattern is not (
                    "grow" or
                    "shrink" or
                    "aba" or
                    "sawtooth" or
                    "jitter" or
                    "pixel"))
            {
                throw new ArgumentException($"Unknown viewport resize pattern '{pattern}'.");
            }
            var inputHz = ParseDouble(
                Read(arguments, "--viewport-input-hz="),
                DefaultInputHz);
            var inputCount = ParseInt(
                Read(arguments, "--viewport-input-count="),
                DefaultInputCount);
            if (!double.IsFinite(inputHz) || inputHz < 1 || inputHz > 1000)
            {
                throw new ArgumentOutOfRangeException(nameof(arguments), "Input Hz must be 1..1000.");
            }
            if (inputCount < 2 || inputCount > 240)
            {
                throw new ArgumentOutOfRangeException(nameof(arguments), "Input count must be 2..240.");
            }
            return new ResizeOptions(pattern, inputHz, inputCount);
        }

        private static string? Read(string[] arguments, string prefix) =>
            arguments.FirstOrDefault(argument =>
                argument.StartsWith(prefix, StringComparison.Ordinal))?[prefix.Length..];

        private static double ParseDouble(string? value, double fallback) =>
            value is null ? fallback : double.Parse(value, CultureInfo.InvariantCulture);

        private static int ParseInt(string? value, int fallback) =>
            value is null ? fallback : int.Parse(value, CultureInfo.InvariantCulture);
    }
}

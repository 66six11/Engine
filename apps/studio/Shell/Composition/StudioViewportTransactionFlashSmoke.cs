using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionFlashSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-transaction-flash";
    private static readonly TimeSpan kTimeout = TimeSpan.FromSeconds(10);
    private const double kLayoutEpsilon = 0.01;

    public static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        var exitCode = 1;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession(
                "viewport-transaction-flash.scene.json");
            var control = host.CreateControl(
                session,
                isRealtime: false,
                testHooks: new ViewportCompositionControlTestHooks
                {
                    EnableFlashSentinelCorners = true,
                });
            var layout = StudioViewportDockSmokeLayout.Create(control);
            var recorder = new FlashCompositionBatchRecorder(control);
            layout.Splitter.ConfigurePresentationTransactionTestHooks(
                new ViewportPresentationTransactionTestHooks
                {
                    WrapGroupRendered = recorder.ObserveRenderedBatchAsync,
                });

            host.Show(desktop, layout.Root, "Viewport Transaction Flash Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync([control], minimumFrames: 1);
            var originWidth = layout.First.ActualWidth;
            if (!double.IsFinite(originWidth) || originWidth <= 160)
            {
                throw new InvalidOperationException(
                    "The flash smoke did not receive a renderable initial dock layout.");
            }

            var requestedWidths = new[]
            {
                originWidth + 96,
                originWidth + 24,
                originWidth + 128,
                originWidth + 48,
                originWidth + 112,
                originWidth + 32,
                originWidth + 144,
                originWidth + 64,
            };
            var measurement = control.BeginResizeMeasurement();
            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                for (var index = 0; index < requestedWidths.Length; index++)
                {
                    var expectedBatchCount = recorder.Count + 1;
                    resize.RequestCumulative(
                        requestedWidths[index] - originWidth,
                        isFinal: index == requestedWidths.Length - 1);
                    await resize.WhenIdleAsync().WaitAsync(kTimeout);
                    await recorder.WaitForCountAsync(expectedBatchCount, kTimeout);
                }
            }

            if (layout.Splitter.LatestRetirementCompletion is { } retirement)
            {
                var retirementReport = await retirement.WaitAsync(kTimeout);
                if (!retirementReport.Succeeded)
                {
                    throw new InvalidOperationException(
                        $"The final flash transaction retired as {retirementReport.Result}: " +
                        retirementReport.Failure);
                }
            }

            var batches = recorder.Capture();
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            var resizeMetrics = control.CaptureResizeMeasurement(measurement);
            var invalidBatches = batches.Where(static batch => !batch.StructurallyExact).ToArray();
            if (batches.Count != requestedWidths.Length ||
                invalidBatches.Length != 0 ||
                metrics.HasOverflowed ||
                metrics.RejectedEventCount != 0 ||
                metrics.UniquePublishedGenerationCount != requestedWidths.Length ||
                metrics.UniqueRenderedGenerationCount != requestedWidths.Length ||
                metrics.Outcomes != default ||
                resizeMetrics.RequestedMismatchHiddenDuration != TimeSpan.Zero)
            {
                throw new InvalidOperationException(
                    $"flash transaction acceptance failed: batches={batches.Count}/" +
                    $"{requestedWidths.Length}, invalid={invalidBatches.Length}, " +
                    $"published={metrics.UniquePublishedGenerationCount}, " +
                    $"rendered={metrics.UniqueRenderedGenerationCount}, " +
                    $"overflow={metrics.OverflowCount}, rejected={metrics.RejectedEventCount}, " +
                    $"hidden={resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}.");
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                "flash-structural",
                metrics,
                resizeMetrics.RequestedMismatchHiddenDutyCycle);
            foreach (var batch in batches)
            {
                WriteBatch(batch);
            }
            Console.Out.WriteLine(
                "viewport-transaction-flash " + JsonSerializer.Serialize(new
                {
                    scenario = "flash-structural",
                    evidenceKind = "transaction-batch-structural",
                    compositionBatchCount = batches.Count,
                    sentinel = new
                    {
                        enabled = true,
                        owner = "scene-viewport-native-surface",
                        activation = "typed-native-present-diagnostic-flag",
                        corners = new[] { "magenta", "green", "cyan", "yellow" },
                    },
                    structuralEvidence = new
                    {
                        exact = batches.Count(static batch => batch.StructurallyExact),
                        outOfBounds = batches.Count(static batch => batch.OutOfBounds),
                        blank = batches.Count(static batch => batch.Blank),
                        stretch = batches.Count(static batch => batch.Stretch),
                        crop = batches.Count(static batch => batch.Crop),
                        extentMismatch = batches.Count(static batch => batch.ExtentMismatch),
                    },
                    pixelEvidenceAvailable = false,
                    physicalDisplayedEvidenceAvailable = false,
                    pixelEvidenceReason =
                        "This process has no reliable compositor/window pixel capture. " +
                        "Sentinel pixels are written into the native Scene surface, but this " +
                        "run claims structural composition evidence only.",
                }));
            Console.Out.WriteLine(
                $"viewport-transaction-flash-structural PASS: {batches.Count} " +
                "transaction-batch structural observations were exact with no " +
                "out-of-bounds, blank, stretch, or crop; pixelEvidenceAvailable=false; " +
                "physicalDisplayedEvidenceAvailable=false.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"viewport-transaction-flash FAIL: {exception.Message}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static void WriteBatch(FlashCompositionBatch batch)
    {
        Console.Out.WriteLine(
            "viewport-transaction-flash-batch " + JsonSerializer.Serialize(new
            {
                batch = batch.Sequence,
                renderedQpc = batch.RenderedTimestamp,
                bounds = new
                {
                    width = batch.BoundsWidth,
                    height = batch.BoundsHeight,
                    physicalWidth = batch.BoundsExtent.Width,
                    physicalHeight = batch.BoundsExtent.Height,
                },
                frontExtent = Extent(batch.FrontExtent),
                candidateExtent = Extent(batch.CandidateExtent),
                visualSize = new
                {
                    width = batch.VisualWidth,
                    height = batch.VisualHeight,
                },
                visualExtent = Extent(batch.VisualExtent),
                surfaceExtent = Extent(batch.SurfaceExtent),
                opacity = batch.Opacity,
                endpoint = batch.Identity.EndpointId.Value,
                session = batch.Identity.SessionId.Value,
                epoch = batch.Identity.Epoch,
                transaction = batch.Identity.TransactionId.Value,
                generation = batch.Identity.Generation,
                requestedExtent = Extent(batch.Identity.Extent),
                outOfBounds = batch.OutOfBounds,
                blank = batch.Blank,
                stretch = batch.Stretch,
                crop = batch.Crop,
                extentMismatch = batch.ExtentMismatch,
                structurallyExact = batch.StructurallyExact,
            }));
    }

    private static object Extent(ViewportExtent extent) => new
    {
        width = extent.Width,
        height = extent.Height,
    };

    private sealed class FlashCompositionBatchRecorder
    {
        private readonly object gate_ = new();
        private readonly ViewportCompositionControl control_;
        private readonly List<FlashCompositionBatch> batches_ = [];

        public FlashCompositionBatchRecorder(ViewportCompositionControl control)
        {
            control_ = control ?? throw new ArgumentNullException(nameof(control));
        }

        public int Count
        {
            get
            {
                lock (gate_)
                {
                    return batches_.Count;
                }
            }
        }

        public async Task ObserveRenderedBatchAsync(
            Task rendered,
            ViewportPresentationTransactionGroupHookContext context)
        {
            ArgumentNullException.ThrowIfNull(rendered);
            ArgumentNullException.ThrowIfNull(context);
            await rendered;
            FlashCompositionBatch captured;
            if (Dispatcher.UIThread.CheckAccess())
            {
                captured = CaptureOnUiThread(context);
            }
            else
            {
                captured = await Dispatcher.UIThread.InvokeAsync(
                    () => CaptureOnUiThread(context),
                    DispatcherPriority.Render);
            }
            lock (gate_)
            {
                batches_.Add(captured with { Sequence = batches_.Count + 1 });
            }
        }

        public async Task WaitForCountAsync(int expectedCount, TimeSpan timeout)
        {
            using var deadline = new System.Threading.CancellationTokenSource(timeout);
            while (Count < expectedCount)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(2), deadline.Token);
            }
        }

        public IReadOnlyList<FlashCompositionBatch> Capture()
        {
            lock (gate_)
            {
                return batches_.ToArray();
            }
        }

        private FlashCompositionBatch CaptureOnUiThread(
            ViewportPresentationTransactionGroupHookContext context)
        {
            Dispatcher.UIThread.VerifyAccess();
            var participant = context.Participants.Single();
            var snapshot = control_.CapturePresentationTestSnapshot();
            var renderScaling = TopLevel.GetTopLevel(control_)?.RenderScaling ?? 0;
            var hasBoundsExtent = ViewportPhysicalExtentPolicy.TryCalculate(
                control_.Bounds.Width,
                control_.Bounds.Height,
                renderScaling,
                out var boundsExtent);
            var hasVisualExtent = ViewportPhysicalExtentPolicy.TryCalculate(
                snapshot.VisualSize.X,
                snapshot.VisualSize.Y,
                renderScaling,
                out var visualExtent);
            var blank = snapshot.VisualSurface is null ||
                        snapshot.VisualOpacity <= 0.001F ||
                        snapshot.SurfaceExtent.Width == 0 ||
                        snapshot.SurfaceExtent.Height == 0;
            var outOfBounds =
                snapshot.VisualSize.X > control_.Bounds.Width + kLayoutEpsilon ||
                snapshot.VisualSize.Y > control_.Bounds.Height + kLayoutEpsilon ||
                hasBoundsExtent &&
                (snapshot.SurfaceExtent.Width > boundsExtent.Width ||
                 snapshot.SurfaceExtent.Height > boundsExtent.Height);
            var stretch = !hasVisualExtent || visualExtent != snapshot.SurfaceExtent;
            var crop = !hasBoundsExtent ||
                       snapshot.SurfaceExtent.Width > boundsExtent.Width ||
                       snapshot.SurfaceExtent.Height > boundsExtent.Height;
            var extentMismatch = !hasBoundsExtent ||
                                 boundsExtent != snapshot.FrontExtent ||
                                 boundsExtent != snapshot.SurfaceExtent ||
                                 boundsExtent != participant.Identity.Extent ||
                                 snapshot.CurrentExtent != snapshot.FrontExtent ||
                                 snapshot.GeometryGeneration != participant.Identity.Generation ||
                                 snapshot.SurfaceGeneration != participant.Identity.Generation ||
                                 !snapshot.HasExactSurface ||
                                 Math.Abs(snapshot.VisualOpacity - 1) > 0.001F;
            return new FlashCompositionBatch(
                Sequence: 0,
                RenderedTimestamp: Stopwatch.GetTimestamp(),
                BoundsWidth: control_.Bounds.Width,
                BoundsHeight: control_.Bounds.Height,
                BoundsExtent: boundsExtent,
                FrontExtent: snapshot.FrontExtent,
                CandidateExtent: snapshot.CandidateExtent,
                VisualWidth: snapshot.VisualSize.X,
                VisualHeight: snapshot.VisualSize.Y,
                VisualExtent: visualExtent,
                SurfaceExtent: snapshot.SurfaceExtent,
                Opacity: snapshot.VisualOpacity,
                Identity: participant.Identity,
                OutOfBounds: outOfBounds,
                Blank: blank,
                Stretch: stretch,
                Crop: crop,
                ExtentMismatch: extentMismatch);
        }
    }

    private sealed record FlashCompositionBatch(
        int Sequence,
        long RenderedTimestamp,
        double BoundsWidth,
        double BoundsHeight,
        ViewportExtent BoundsExtent,
        ViewportExtent FrontExtent,
        ViewportExtent CandidateExtent,
        double VisualWidth,
        double VisualHeight,
        ViewportExtent VisualExtent,
        ViewportExtent SurfaceExtent,
        float Opacity,
        ViewportPresentationTelemetryIdentity Identity,
        bool OutOfBounds,
        bool Blank,
        bool Stretch,
        bool Crop,
        bool ExtentMismatch)
    {
        public bool StructurallyExact =>
            !OutOfBounds && !Blank && !Stretch && !Crop && !ExtentMismatch;
    }
}

using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;

namespace Editor.Shell.Composition;

internal static class StudioViewportCadenceSmoke
{
    internal const string CommandLineSwitch = "--smoke-studio-viewport-cadence";
    internal const double MinimumFramesPerSecond = 60;
    private const int MinimumObservedResizeGenerations = 72;
    private const ulong WarmUpFrameCount = 120;
    private static readonly TimeSpan kWarmUpTimeout = TimeSpan.FromSeconds(20);
    private static readonly TimeSpan kMeasurementDuration = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan kMaximumP95FrameInterval =
        TimeSpan.FromMilliseconds(25);
    private static readonly TimeSpan kMaximumResizeP95UniqueCompletionInterval =
        TimeSpan.FromMilliseconds(25);
    private static readonly TimeSpan kMaximumResizeBoundsToCompletion =
        TimeSpan.FromMilliseconds(25);
    private static readonly TimeSpan kMinimumResizeMeasurementDuration =
        TimeSpan.FromMilliseconds(500);
    private static readonly TimeSpan kMaximumFrameInterval =
        TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan kOnDemandFrameTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan kOnDemandStableDuration = TimeSpan.FromMilliseconds(150);

    public static bool IsRequested(string[] arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        return arguments.Contains(CommandLineSwitch, StringComparer.Ordinal);
    }

    public static async Task RunAsync(IClassicDesktopStyleApplicationLifetime desktop)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        var exitCode = 1;
        var lifetime = new ViewportPresentationLifetime();
        var runtime = new ViewportRuntimeBridge();
        ViewportSession? session = null;
        ViewportSession? replacementSession = null;
        ViewportCompositionControl? control = null;
        Window? window = null;
        try
        {
            var warmUpFailure = await runtime.WarmUpAsync();
            if (warmUpFailure is not null)
            {
                throw new InvalidOperationException(warmUpFailure.Message);
            }

            session = new ViewportSession(
                ViewportSessionId.Create(),
                ViewportRenderKind.Scene,
                new SceneDocumentSnapshot(
                    Guid.NewGuid(),
                    "studio-viewport-cadence-smoke.scene.json",
                    revision: 1,
                    savedRevision: 1,
                    entities: []),
                ViewportCameraSnapshot.DefaultScene);
            control = new ViewportCompositionControl
            {
                Session = session,
                Lifetime = lifetime,
                IsRealtime = true,
            };
            var sceneColumn = new ColumnDefinition
            {
                Width = new GridLength(640, GridUnitType.Pixel),
            };
            var content = new Grid();
            content.ColumnDefinitions.Add(sceneColumn);
            content.ColumnDefinitions.Add(new ColumnDefinition
            {
                Width = new GridLength(5, GridUnitType.Pixel),
            });
            content.ColumnDefinitions.Add(new ColumnDefinition
            {
                Width = new GridLength(1, GridUnitType.Star),
            });
            content.Children.Add(control);
            var splitter = new GridSplitter();
            Grid.SetColumn(splitter, 1);
            content.Children.Add(splitter);
            var siblingPanel = new Border();
            Grid.SetColumn(siblingPanel, 2);
            content.Children.Add(siblingPanel);
            window = new Window
            {
                Width = 1280,
                Height = 720,
                Title = "Asharia Studio Viewport Cadence Smoke",
                Content = content,
            };
            desktop.MainWindow = window;
            window.Show();

            using (var deadline = new CancellationTokenSource(kWarmUpTimeout))
            {
                while (control.PresentationMetrics.TotalPresentedFrames < WarmUpFrameCount &&
                       !control.IsDegraded)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(10), deadline.Token);
                }
            }
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }

            // Exercise the same layout boundary as a Studio dock splitter. Every distinct panel
            // pixel extent replaces the exact-size external-image generation; no bucket padding,
            // source crop, or retained-image stretch is accepted as a new frame.
            var resizeStartFrame = control.PresentationMetrics.TotalPresentedFrames;
            var resizeStartGeometry = control.PresentationGeometryMetrics;
            var resizeMeasurement = control.BeginResizeMeasurement();
            WritePhaseMarker("resize_begin", Stopwatch.GetTimestamp());
            for (var step = 0; step < 90; step++)
            {
                sceneColumn.Width = new GridLength(
                    420 + step * 8,
                    GridUnitType.Pixel);
                if (step < 89)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(8));
                }
            }
            var (resizeMetrics, resizeCatchUpBatches) =
                await WaitForFinalResizeGenerationAsync(
                    control,
                    resizeMeasurement,
                    expectedBoundsWidth: 420 + 89 * 8);
            WritePhaseMarker("resize_end", Stopwatch.GetTimestamp());
            var afterActiveResize = control.PresentationMetrics;
            var framesDuringResize = checked(
                afterActiveResize.TotalPresentedFrames - resizeStartFrame);
            var exactFramesDuringResize = checked(
                control.PresentationGeometryMetrics.ExactExtentPresentedFrames -
                resizeStartGeometry.ExactExtentPresentedFrames);
            var resizeGeometry = control.PresentationGeometryMetrics;
            if (resizeMetrics.RingOverflowed ||
                resizeMetrics.TrackerResetSinceMeasurement ||
                resizeMetrics.ContainsNonBoundsGeometryChanges ||
                resizeMetrics.ObservedBoundsGenerations < MinimumObservedResizeGenerations ||
                resizeMetrics.WindowElapsed < kMinimumResizeMeasurementDuration ||
                resizeMetrics.UniqueExactSubmittedPerSecond < MinimumFramesPerSecond ||
                resizeMetrics.UniqueExactCompletedPerSecond < MinimumFramesPerSecond ||
                exactFramesDuringResize != framesDuringResize ||
                !resizeGeometry.CurrentSurfaceIsExact ||
                !resizeMetrics.FinalGenerationHasExactSurface ||
                !resizeMetrics.FinalGenerationCompleted ||
                resizeMetrics.P95UniqueCompletionInterval >
                    kMaximumResizeP95UniqueCompletionInterval ||
                resizeMetrics.MaximumUniqueCompletionInterval > kMaximumFrameInterval ||
                resizeMetrics.P95BoundsToExactCompletion >
                    kMaximumResizeBoundsToCompletion)
            {
                throw new InvalidOperationException(
                    $"Studio viewport did not keep unique exact generations during active panel " +
                    $"resize: {resizeMetrics.UniqueExactCompletedGenerations}/" +
                    $"{resizeMetrics.ObservedBoundsGenerations} generations completed at " +
                    $"{resizeMetrics.UniqueExactCompletedPerSecond:F2} per second " +
                    $"({resizeMetrics.UniqueExactSubmittedGenerations} submitted at " +
                    $"{resizeMetrics.UniqueExactSubmittedPerSecond:F2}/s, " +
                    $"coverage {resizeMetrics.CompletionCoverage:P1}) in " +
                    $"{resizeMetrics.WindowElapsed.TotalSeconds:F2} s; " +
                    $"{framesDuringResize} total accepted frames, {exactFramesDuringResize} exact, " +
                    $"rejected non-exact candidates " +
                    $"{resizeGeometry.RejectedNonExactCandidates - resizeStartGeometry.RejectedNonExactCandidates}, " +
                    $"unique interval p95 " +
                    $"{resizeMetrics.P95UniqueCompletionInterval.TotalMilliseconds:F2} ms, " +
                    $"max {resizeMetrics.MaximumUniqueCompletionInterval.TotalMilliseconds:F2} ms, " +
                    $"Bounds-to-completion p95 " +
                    $"{resizeMetrics.P95BoundsToExactCompletion.TotalMilliseconds:F2} ms, " +
                    $"hidden duty {resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}, " +
                    $"tracker reset {resizeMetrics.TrackerResetSinceMeasurement}, " +
                    $"mixed geometry sources {resizeMetrics.ContainsNonBoundsGeometryChanges}, " +
                    $"final catch-up {resizeCatchUpBatches}/2 additional rendered batches.");
            }
            // Return to the original A extent after the monotonic A->B resize. A matching old
            // bitmap must stay hidden until a newly rendered A generation reaches the surface.
            var returnMeasurement = control.BeginResizeMeasurement();
            sceneColumn.Width = new GridLength(640, GridUnitType.Pixel);
            var (returnMetrics, _) =
                await WaitForFinalResizeGenerationAsync(
                    control,
                    returnMeasurement,
                    expectedBoundsWidth: 640);
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            var returnedGeometry = control.PresentationGeometryMetrics;
            if (returnMetrics.UniqueExactCompletedGenerations != 1 ||
                !returnMetrics.FinalGenerationCompleted ||
                !returnedGeometry.CurrentSurfaceIsExact)
            {
                throw new InvalidOperationException(
                    "Studio viewport did not publish a new exact generation after returning " +
                    "to the initial panel extent.");
            }

            var measuredAt = Stopwatch.GetTimestamp();
            WritePhaseMarker("steady_begin", measuredAt);
            var first = control.PresentationMetrics;
            var firstGeometry = control.PresentationGeometryMetrics;
            await Task.Delay(kMeasurementDuration);
            var steadyEndedAt = Stopwatch.GetTimestamp();
            WritePhaseMarker("steady_end", steadyEndedAt);
            var elapsed = Stopwatch.GetElapsedTime(measuredAt, steadyEndedAt);
            var last = control.PresentationMetrics;
            var measuredFrames = checked(last.TotalPresentedFrames - first.TotalPresentedFrames);
            var lastGeometry = control.PresentationGeometryMetrics;
            var measuredExactFrames = checked(
                lastGeometry.ExactExtentPresentedFrames -
                firstGeometry.ExactExtentPresentedFrames);
            var measuredFramesPerSecond = measuredFrames / elapsed.TotalSeconds;
            if (!last.MeetsMinimumFramesPerSecond(MinimumFramesPerSecond) ||
                measuredFramesPerSecond < MinimumFramesPerSecond ||
                measuredExactFrames != measuredFrames ||
                !lastGeometry.LastPresentationIsExact ||
                last.P95FrameInterval > kMaximumP95FrameInterval ||
                last.MaximumFrameInterval > kMaximumFrameInterval)
            {
                throw new InvalidOperationException(
                    $"Studio viewport surface-update cadence was " +
                    $"{measuredFramesPerSecond:F2} FPS " +
                    $"({last.FramesPerSecond:F2} FPS bounded window), " +
                    $"{measuredExactFrames}/{measuredFrames} exact-size frames, " +
                    $"rejected non-exact candidates " +
                    $"{lastGeometry.RejectedNonExactCandidates - firstGeometry.RejectedNonExactCandidates}, " +
                    $"p95 {last.P95FrameInterval.TotalMilliseconds:F2} ms, " +
                    $"max {last.MaximumFrameInterval.TotalMilliseconds:F2} ms; required " +
                    $">= {MinimumFramesPerSecond:F0} FPS, p95 <= " +
                    $"{kMaximumP95FrameInterval.TotalMilliseconds:F0} ms and max <= " +
                    $"{kMaximumFrameInterval.TotalMilliseconds:F0} ms.");
            }

            // Unity-style dirty refresh is an explicit secondary mode. It must become idle when
            // the immutable view state is clean, wake for a camera change, and request a fresh
            // exact frame when an ancestor-hidden dock tab becomes visible again.
            control.IsRealtime = false;
            var onDemandIdleFrame = await WaitUntilPresentationSettlesAsync(control);
            await Task.Delay(kOnDemandStableDuration);
            if (control.PresentationMetrics.TotalPresentedFrames != onDemandIdleFrame)
            {
                throw new InvalidOperationException(
                    "Studio viewport continued rendering after OnDemand reached a clean state.");
            }

            session.SetCamera(new ViewportCameraSnapshot(
                new Asharia.Runtime.Float3(2, 3, -7),
                Asharia.Runtime.Float3.Zero,
                new Asharia.Runtime.Float3(0, 1, 0),
                MathF.PI / 3,
                0.1f,
                1000.0f));
            var cameraFrame = await WaitForNewExactFrameAsync(control, onDemandIdleFrame);
            var cameraSettledFrame = await WaitUntilPresentationSettlesAsync(control);
            await Task.Delay(kOnDemandStableDuration);
            if (control.PresentationMetrics.TotalPresentedFrames != cameraSettledFrame)
            {
                throw new InvalidOperationException(
                    "Studio viewport did not return to idle after an OnDemand camera refresh.");
            }

            content.IsVisible = false;
            var hiddenFrame = await WaitUntilPresentationSettlesAsync(control);
            await Task.Delay(kOnDemandStableDuration);
            if (control.PresentationMetrics.TotalPresentedFrames != hiddenFrame)
            {
                throw new InvalidOperationException(
                    "Studio viewport presented frames while its dock ancestor was hidden.");
            }
            content.IsVisible = true;
            var exposedFrame = await WaitForNewExactFrameAsync(control, hiddenFrame);

            var beforeLifetimeResume = await WaitUntilPresentationSettlesAsync(control);
            await using (var pause = await lifetime.PauseAndDrainAsync())
            {
            }
            var lifetimeResumeFrame = await WaitForNewExactFrameAsync(
                control,
                beforeLifetimeResume);

            replacementSession = new ViewportSession(
                ViewportSessionId.Create(),
                ViewportRenderKind.Scene,
                new SceneDocumentSnapshot(
                    Guid.NewGuid(),
                    "studio-viewport-session-swap-smoke.scene.json",
                    revision: 1,
                    savedRevision: 1,
                    entities: []),
                ViewportCameraSnapshot.DefaultScene);
            var currentSize = control.PresentationGeometryMetrics.LastPresentedSize;
            if (!replacementSession.TryPublishLatest(currentSize, out _))
            {
                throw new InvalidOperationException(
                    "Studio viewport could not prepare a clean replacement session.");
            }
            var beforeSessionSwap = await WaitUntilPresentationSettlesAsync(control);
            control.Session = null;
            var childVisual = ElementComposition.GetElementChildVisual(control);
            if (childVisual is not null && childVisual.Opacity != 0)
            {
                throw new InvalidOperationException(
                    "Studio viewport kept the previous session surface visible after removal.");
            }
            await Task.Delay(kOnDemandStableDuration);
            if (control.PresentationMetrics.TotalPresentedFrames != beforeSessionSwap)
            {
                throw new InvalidOperationException(
                    "Studio viewport presented a frame without an attached session.");
            }
            control.Session = replacementSession;
            var replacementFrame = await WaitForNewExactFrameAsync(
                control,
                beforeSessionSwap);

            Console.Out.WriteLine(
                $"Studio viewport surface-update cadence PASS: " +
                $"{measuredFramesPerSecond:F2} FPS measured, " +
                $"{last.FramesPerSecond:F2} FPS bounded window, " +
                $"p95 {last.P95FrameInterval.TotalMilliseconds:F2} ms, " +
                $"max {last.MaximumFrameInterval.TotalMilliseconds:F2} ms, " +
                $"{measuredFrames} frames in {elapsed.TotalSeconds:F2} s; resize kept " +
                $"{resizeMetrics.UniqueExactCompletedGenerations}/" +
                $"{resizeMetrics.ObservedBoundsGenerations} unique exact generations at " +
                $"{resizeMetrics.UniqueExactCompletedPerSecond:F2}/s " +
                $"({resizeMetrics.UniqueExactSubmittedGenerations} submitted, " +
                $"coverage {resizeMetrics.CompletionCoverage:P1}, p95 interval " +
                $"{resizeMetrics.P95UniqueCompletionInterval.TotalMilliseconds:F2} ms, " +
                $"Bounds-to-submit " +
                $"{resizeMetrics.P95BoundsToExactSubmit.TotalMilliseconds:F2} ms, " +
                $"Bounds-to-completion " +
                $"{resizeMetrics.P95BoundsToExactCompletion.TotalMilliseconds:F2} ms, " +
                $"hidden duty {resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}, " +
                $"catch-up {resizeCatchUpBatches}/2 additional rendered batches); " +
                $"OnDemand camera/expose/lifetime-resume frames " +
                $"{cameraFrame}/{exposedFrame}/{lifetimeResumeFrame}, replacement frame " +
                $"{replacementFrame}.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"Studio viewport surface-update cadence FAIL: {exception.Message}");
        }
        finally
        {
            try
            {
                if (control is not null)
                {
                    control.Session = null;
                }
                if (window is not null)
                {
                    window.Content = null;
                    window.Close();
                }
                session?.Close();
                replacementSession?.Close();
                await lifetime.StopAndDrainAsync();
                runtime.Shutdown();
            }
            catch (Exception cleanupFailure)
            {
                Console.Error.WriteLine(
                    $"Studio viewport cadence cleanup FAIL: {cleanupFailure.Message}");
                exitCode = 1;
            }
            desktop.Shutdown(exitCode);
        }
    }

    private static async Task<ulong> WaitUntilPresentationSettlesAsync(
        ViewportCompositionControl control)
    {
        using var deadline = new CancellationTokenSource(kOnDemandFrameTimeout);
        var lastFrame = control.PresentationMetrics.TotalPresentedFrames;
        var stableSince = Stopwatch.GetTimestamp();
        while (Stopwatch.GetElapsedTime(stableSince) < kOnDemandStableDuration)
        {
            await Task.Delay(TimeSpan.FromMilliseconds(10), deadline.Token);
            var currentFrame = control.PresentationMetrics.TotalPresentedFrames;
            if (currentFrame == lastFrame)
            {
                continue;
            }
            lastFrame = currentFrame;
            stableSince = Stopwatch.GetTimestamp();
        }
        return lastFrame;
    }

    private static void WritePhaseMarker(string phase, long timestamp) =>
        Console.Out.WriteLine(
            $"Studio viewport phase {phase}: QPC={timestamp}, " +
            $"Frequency={Stopwatch.Frequency}.");

    private static async Task<(ViewportResizePresentationMetrics Metrics, int RenderedBatches)>
        WaitForFinalResizeGenerationAsync(
            ViewportCompositionControl control,
            ViewportResizeMeasurementToken measurement,
            double expectedBoundsWidth)
    {
        using var deadline = new CancellationTokenSource(kOnDemandFrameTimeout);
        ViewportPresentationGeometryMetrics geometry;
        ViewportResizePresentationMetrics metrics;
        while (true)
        {
            geometry = control.PresentationGeometryMetrics;
            metrics = control.CaptureResizeMeasurement(measurement);
            if (metrics.TrackerResetSinceMeasurement || metrics.RingOverflowed)
            {
                throw new InvalidOperationException(
                    "Studio viewport resize diagnostics were reset or overflowed before " +
                    "the final Bounds generation was observed.");
            }
            if (metrics.ObservedBoundsGenerations > 0 &&
                Math.Abs(control.Bounds.Width - expectedBoundsWidth) < 0.01)
            {
                break;
            }
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }

        var finalGeometryGeneration = geometry.CurrentGeometryGeneration;
        var renderedBatches = 0;
        var visual = ElementComposition.GetElementVisual(control) ??
            throw new InvalidOperationException(
                "Studio viewport composition visual is unavailable during resize.");
        while (!geometry.CurrentSurfaceIsExact && renderedBatches < 2)
        {
            var batch = visual.Compositor.RequestCompositionBatchCommitAsync();
            await batch.Rendered.WaitAsync(deadline.Token);
            renderedBatches++;
            geometry = control.PresentationGeometryMetrics;
            if (geometry.CurrentGeometryGeneration != finalGeometryGeneration)
            {
                throw new InvalidOperationException(
                    "Studio viewport geometry changed after the final resize generation " +
                    "was selected for the two-batch catch-up gate.");
            }
        }
        if (!geometry.CurrentSurfaceIsExact)
        {
            throw new InvalidOperationException(
                "Studio viewport final exact surface did not catch up within two Avalonia " +
                "rendered composition batches.");
        }

        while (true)
        {
            metrics = control.CaptureResizeMeasurement(measurement);
            if (metrics.FinalGeometryGeneration != finalGeometryGeneration)
            {
                throw new InvalidOperationException(
                    "Studio viewport geometry changed while waiting for the final exact " +
                    "surface completion.");
            }
            if (metrics.FinalGenerationCompleted &&
                metrics.FinalGenerationHasExactSurface)
            {
                return (metrics, renderedBatches);
            }
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static async Task<ulong> WaitForNewExactFrameAsync(
        ViewportCompositionControl control,
        ulong previousFrame)
    {
        using var deadline = new CancellationTokenSource(kOnDemandFrameTimeout);
        while (control.PresentationMetrics.TotalPresentedFrames <= previousFrame ||
               !control.PresentationGeometryMetrics.LastPresentationIsExact)
        {
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            await Task.Delay(TimeSpan.FromMilliseconds(10), deadline.Token);
        }
        return control.PresentationMetrics.TotalPresentedFrames;
    }
}

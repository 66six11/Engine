using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

/// <summary>
/// Measures foreground realtime Scene View surface-update cadence either at rest or while a
/// dedicated camera-navigation lane supplies input. Resize, overload, fault, supersede,
/// lifecycle, and multi-endpoint behavior belong to their viewport transaction smoke families.
/// </summary>
internal static class StudioViewportCadenceSmoke
{
    internal const string CommandLineSwitch = "--smoke-studio-viewport-cadence";
    internal const string CameraNavigationCommandLineSwitch =
        "--smoke-studio-camera-navigation-cadence";
    internal const double MinimumFramesPerSecond = 60;
    private const int CameraInputRate = 240;
    private const ulong WarmUpFrameCount = 120;
    private static readonly TimeSpan kMeasurementDuration = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan kMaximumP95FrameInterval =
        TimeSpan.FromMilliseconds(25);
    private static readonly TimeSpan kMaximumFrameInterval =
        TimeSpan.FromMilliseconds(100);

    public static bool IsRequested(string[] arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        return Array.IndexOf(arguments, CommandLineSwitch) >= 0;
    }

    public static bool IsCameraNavigationRequested(string[] arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        return Array.IndexOf(arguments, CameraNavigationCommandLineSwitch) >= 0;
    }

    public static Task RunAsync(IClassicDesktopStyleApplicationLifetime desktop) =>
        RunAsync(desktop, driveCamera: false);

    public static Task RunCameraNavigationAsync(
        IClassicDesktopStyleApplicationLifetime desktop) =>
        RunAsync(desktop, driveCamera: true);

    private static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        bool driveCamera)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        var exitCode = 1;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession("studio-viewport-cadence-smoke.scene.json");
            var measurement = new ViewportCadenceMeasurement();
            var control = host.CreateControl(session, isRealtime: true,
                testHooks: new ViewportCompositionControlTestHooks { FramePresented = measurement.Record });
            var layout = StudioViewportDockSmokeLayout.Create(control);
            host.Show(desktop, layout.Root, "Studio Viewport Realtime Cadence Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync(
                [control],
                WarmUpFrameCount);

            var geometryBefore = control.PresentationGeometryMetrics;
            var measuredAt = Stopwatch.GetTimestamp();
            measurement.Begin(measuredAt);
            WritePhaseMarker(driveCamera ? "camera_begin" : "steady_begin", measuredAt);
            var cameraInputCount = driveCamera
                ? await Task.Run(() => DriveCameraAsync(session, kMeasurementDuration))
                : 0;
            if (!driveCamera)
            {
                await Task.Delay(kMeasurementDuration);
            }
            var endedAt = Stopwatch.GetTimestamp();
            WritePhaseMarker(driveCamera ? "camera_end" : "steady_end", endedAt);

            var cadence = measurement.End(endedAt);
            var geometryAfter = control.PresentationGeometryMetrics;
            var elapsed = Stopwatch.GetElapsedTime(measuredAt, endedAt);
            var measuredFrames = cadence.Frames;
            var measuredRate = elapsed <= TimeSpan.Zero
                ? 0
                : measuredFrames / elapsed.TotalSeconds;
            var rejected = checked(
                geometryAfter.RejectedNonExactCandidates -
                geometryBefore.RejectedNonExactCandidates);

            if (measuredRate < MinimumFramesPerSecond ||
                cadence.P95FrameInterval > kMaximumP95FrameInterval ||
                cadence.MaximumFrameInterval > kMaximumFrameInterval ||
                rejected != 0 ||
                !geometryAfter.CurrentSurfaceIsExact ||
                !geometryAfter.LastPresentationIsExact ||
                control.IsDegraded)
            {
                throw new InvalidOperationException(
                    $"{(driveCamera ? "camera-navigation" : "steady realtime")} " +
                    "cadence acceptance failed: " +
                    $"{measuredRate:F2} surface updates/s, " +
                    $"p95={cadence.P95FrameInterval.TotalMilliseconds:F2}ms, " +
                    $"max={cadence.MaximumFrameInterval.TotalMilliseconds:F2}ms, " +
                    $"exact={geometryAfter.CurrentSurfaceIsExact}, " +
                    $"mismatch={rejected}, degraded={control.IsDegraded}.");
            }

            if (driveCamera)
            {
                await WaitForCurrentCameraPresentationAsync(control, session);
            }

            Console.Out.WriteLine(
                $"Studio viewport {(driveCamera ? "camera-navigation" : "steady")} " +
                "surface-update cadence PASS: " +
                $"{measuredRate:F2} FPS, p95 " +
                $"{cadence.P95FrameInterval.TotalMilliseconds:F2} ms, max " +
                $"{cadence.MaximumFrameInterval.TotalMilliseconds:F2} ms, " +
                $"{measuredFrames} frames/{elapsed.TotalSeconds:F2}s, " +
                $"cameraInputs={cameraInputCount}, exact=true. " +
                "This is Avalonia consumer/surface-update completion cadence, not physical " +
                "display evidence.");
            exitCode = 0;
        }
        catch (OperationCanceledException exception)
        {
            Console.Error.WriteLine(
                $"Studio viewport {(driveCamera ? "camera-navigation" : "steady")} " +
                $"cadence FAIL: timed out: {exception.Message}");
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"Studio viewport {(driveCamera ? "camera-navigation" : "steady")} " +
                $"cadence FAIL: {exception.Message}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static void WritePhaseMarker(string phase, long timestamp) =>
        Console.Out.WriteLine(
            $"Studio viewport phase {phase}: QPC={timestamp}, " +
            $"Frequency={Stopwatch.Frequency}.");

    private static async Task<int> DriveCameraAsync(
        ViewportSession session,
        TimeSpan duration)
    {
        var inputCount = checked((int)(duration.TotalSeconds * CameraInputRate));
        var startedAt = Stopwatch.GetTimestamp();
        var actualInputs = new long[inputCount];
        for (var inputIndex = 0; inputIndex < inputCount; inputIndex++)
        {
            var due = TimeSpan.FromSeconds((inputIndex + 1.0) / CameraInputRate);
            while (Stopwatch.GetElapsedTime(startedAt) < due)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(1)).ConfigureAwait(false);
            }

            var camera = session.Camera;
            actualInputs[inputIndex] = Stopwatch.GetTimestamp();
            session.SetCamera(ViewportSceneCameraNavigation.Apply(
                camera,
                new ViewportCameraNavigationDelta(
                    ViewportCameraNavigationMode.Orbit,
                    horizontalFraction: 0.0005f,
                    verticalFraction: 0,
                    aspectRatio: 16.0f / 9.0f)));
        }
        var lateness = new double[inputCount];
        double maximumGapMs = 0;
        var burstInputs = 0;
        for (var i = 0; i < inputCount; i++)
        {
            lateness[i] = Stopwatch.GetElapsedTime(startedAt, actualInputs[i]).TotalMilliseconds -
                (i + 1.0) * 1000 / CameraInputRate;
            if (i == 0) continue;
            var gapMs = Stopwatch.GetElapsedTime(actualInputs[i - 1], actualInputs[i]).TotalMilliseconds;
            maximumGapMs = Math.Max(maximumGapMs, gapMs);
            if (gapMs < 1000.0 / CameraInputRate / 4) burstInputs++;
        }
        Array.Sort(lateness);
        Console.Out.WriteLine($"Camera input timing: targetHz={CameraInputRate}, " +
            $"actualCount={inputCount}, p95LatenessMs={lateness[(int)Math.Ceiling(inputCount * .95) - 1]:F3}, " +
            $"maximumGapMs={maximumGapMs:F3}, burstInputs={burstInputs}. " +
            "Target rate does not imply uniformly spaced input.");
        return inputCount;
    }

    private static async Task WaitForCurrentCameraPresentationAsync(
        ViewportCompositionControl control,
        ViewportSession session)
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (!control.TryCapturePresentedInteractionContext(out var context) ||
               !session.TryCapturePickSnapshot(
                   context.SessionId,
                   context.TargetId,
                   context.TargetRevision,
                   context.FrameSequence,
                   out _))
        {
            await Task.Delay(TimeSpan.FromMilliseconds(5), deadline.Token);
        }
    }
}

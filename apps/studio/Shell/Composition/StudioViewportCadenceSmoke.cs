using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

/// <summary>
/// Measures only the foreground realtime Scene View steady-state surface-update cadence.
/// Resize, overload, fault, supersede, lifecycle, and multi-endpoint behavior belong to their
/// dedicated viewport transaction smoke families.
/// </summary>
internal static class StudioViewportCadenceSmoke
{
    internal const string CommandLineSwitch = "--smoke-studio-viewport-cadence";
    internal const double MinimumFramesPerSecond = 60;
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

    public static async Task RunAsync(IClassicDesktopStyleApplicationLifetime desktop)
    {
        ArgumentNullException.ThrowIfNull(desktop);
        var exitCode = 1;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession("studio-viewport-cadence-smoke.scene.json");
            var control = host.CreateControl(session, isRealtime: true);
            var layout = StudioViewportDockSmokeLayout.Create(control);
            host.Show(desktop, layout.Root, "Studio Viewport Realtime Cadence Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync(
                [control],
                WarmUpFrameCount);

            var geometryBefore = control.PresentationGeometryMetrics;
            var framesBefore = control.PresentationMetrics.TotalPresentedFrames;
            var measuredAt = Stopwatch.GetTimestamp();
            WritePhaseMarker("steady_begin", measuredAt);
            await Task.Delay(kMeasurementDuration);
            var endedAt = Stopwatch.GetTimestamp();
            WritePhaseMarker("steady_end", endedAt);

            var cadence = control.PresentationMetrics;
            var geometryAfter = control.PresentationGeometryMetrics;
            var elapsed = Stopwatch.GetElapsedTime(measuredAt, endedAt);
            var measuredFrames = checked(cadence.TotalPresentedFrames - framesBefore);
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
                    $"steady realtime cadence acceptance failed: " +
                    $"{measuredRate:F2} surface updates/s, " +
                    $"p95={cadence.P95FrameInterval.TotalMilliseconds:F2}ms, " +
                    $"max={cadence.MaximumFrameInterval.TotalMilliseconds:F2}ms, " +
                    $"exact={geometryAfter.CurrentSurfaceIsExact}, " +
                    $"mismatch={rejected}, degraded={control.IsDegraded}.");
            }

            Console.Out.WriteLine(
                "Studio viewport steady surface-update cadence PASS: " +
                $"{measuredRate:F2} FPS, p95 " +
                $"{cadence.P95FrameInterval.TotalMilliseconds:F2} ms, max " +
                $"{cadence.MaximumFrameInterval.TotalMilliseconds:F2} ms, " +
                $"{measuredFrames} frames/{elapsed.TotalSeconds:F2}s, exact=true. " +
                "This is Avalonia consumer/surface-update completion cadence, not physical " +
                "display evidence.");
            exitCode = 0;
        }
        catch (OperationCanceledException exception)
        {
            Console.Error.WriteLine(
                $"Studio viewport steady cadence FAIL: timed out: {exception.Message}");
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"Studio viewport steady cadence FAIL: {exception.Message}");
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
}

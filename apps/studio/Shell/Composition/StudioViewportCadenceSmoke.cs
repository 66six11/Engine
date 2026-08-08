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

namespace Editor.Shell.Composition;

internal static class StudioViewportCadenceSmoke
{
    internal const string CommandLineSwitch = "--smoke-studio-viewport-cadence";
    internal const double MinimumFramesPerSecond = 60;
    private const ulong WarmUpFrameCount = 120;
    private static readonly TimeSpan kWarmUpTimeout = TimeSpan.FromSeconds(20);
    private static readonly TimeSpan kMeasurementDuration = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan kMaximumP95FrameInterval =
        TimeSpan.FromMilliseconds(25);
    private static readonly TimeSpan kMaximumResizeP95FrameInterval =
        TimeSpan.FromMilliseconds(50);
    private static readonly TimeSpan kMaximumFrameInterval =
        TimeSpan.FromMilliseconds(100);

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
            var resizeStartedAt = Stopwatch.GetTimestamp();
            for (var step = 0; step < 90; step++)
            {
                sceneColumn.Width = new GridLength(
                    420 + step * 8,
                    GridUnitType.Pixel);
                await Task.Delay(TimeSpan.FromMilliseconds(8));
            }
            var resizeElapsed = Stopwatch.GetElapsedTime(resizeStartedAt);
            var afterActiveResize = control.PresentationMetrics;
            var framesDuringResize = checked(
                afterActiveResize.TotalPresentedFrames - resizeStartFrame);
            var exactFramesDuringResize = checked(
                control.PresentationGeometryMetrics.ExactExtentPresentedFrames -
                resizeStartGeometry.ExactExtentPresentedFrames);
            var resizeFramesPerSecond = framesDuringResize / resizeElapsed.TotalSeconds;
            var resizeGeometry = control.PresentationGeometryMetrics;
            if (resizeFramesPerSecond < MinimumFramesPerSecond ||
                exactFramesDuringResize != framesDuringResize ||
                !resizeGeometry.LastPresentationIsExact ||
                afterActiveResize.P95FrameInterval > kMaximumResizeP95FrameInterval ||
                afterActiveResize.MaximumFrameInterval > kMaximumFrameInterval)
            {
                throw new InvalidOperationException(
                    $"Studio viewport did not keep exact-size presentation during active panel " +
                    $"resize: {framesDuringResize} frames at {resizeFramesPerSecond:F2} FPS " +
                    $"({exactFramesDuringResize} exact) in {resizeElapsed.TotalSeconds:F2} s, " +
                    $"rejected non-exact candidates " +
                    $"{resizeGeometry.RejectedNonExactCandidates - resizeStartGeometry.RejectedNonExactCandidates}, " +
                    $"p95 {afterActiveResize.P95FrameInterval.TotalMilliseconds:F2} ms, " +
                    $"max {afterActiveResize.MaximumFrameInterval.TotalMilliseconds:F2} ms.");
            }
            // Return to the original A extent after the monotonic A->B resize. A matching old
            // bitmap must stay hidden until a newly rendered A generation reaches the surface.
            var returnToInitialFrame = afterActiveResize.TotalPresentedFrames;
            var returnToInitialExactFrame = resizeGeometry.ExactExtentPresentedFrames;
            sceneColumn.Width = new GridLength(640, GridUnitType.Pixel);
            using (var deadline = new CancellationTokenSource(kWarmUpTimeout))
            {
                while (control.PresentationMetrics.TotalPresentedFrames <
                           returnToInitialFrame + 60 &&
                       !control.IsDegraded)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(10), deadline.Token);
                }
            }
            if (control.IsDegraded)
            {
                throw new InvalidOperationException(control.StatusMessage);
            }
            var returnedGeometry = control.PresentationGeometryMetrics;
            if (returnedGeometry.ExactExtentPresentedFrames <= returnToInitialExactFrame ||
                !returnedGeometry.LastPresentationIsExact)
            {
                throw new InvalidOperationException(
                    "Studio viewport did not publish a new exact generation after returning " +
                    "to the initial panel extent.");
            }

            var measuredAt = Stopwatch.GetTimestamp();
            var first = control.PresentationMetrics;
            var firstGeometry = control.PresentationGeometryMetrics;
            await Task.Delay(kMeasurementDuration);
            var elapsed = Stopwatch.GetElapsedTime(measuredAt);
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

            Console.Out.WriteLine(
                $"Studio viewport surface-update cadence PASS: " +
                $"{measuredFramesPerSecond:F2} FPS measured, " +
                $"{last.FramesPerSecond:F2} FPS bounded window, " +
                $"p95 {last.P95FrameInterval.TotalMilliseconds:F2} ms, " +
                $"max {last.MaximumFrameInterval.TotalMilliseconds:F2} ms, " +
                $"{measuredFrames} frames in {elapsed.TotalSeconds:F2} s; resize kept " +
                $"{framesDuringResize} exact frames at {resizeFramesPerSecond:F2} FPS in " +
                $"{resizeElapsed.TotalSeconds:F2} s.");
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
}

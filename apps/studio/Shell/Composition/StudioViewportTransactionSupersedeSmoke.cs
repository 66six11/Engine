using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionSupersedeSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-transaction-supersede";
    private static readonly TimeSpan kTimeout = TimeSpan.FromSeconds(10);

    public static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        var exitCode = 1;
        Editor.Shell.Views.Docking.EditorDockStagedGridSplitter? observedSplitter = null;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession("viewport-transaction-supersede.scene.json");
            var control = host.CreateControl(session);
            var layout = StudioViewportDockSmokeLayout.Create(control);
            observedSplitter = layout.Splitter;
            var publishAttempt = 0;
            layout.Splitter.ConfigurePresentationTransactionTestHooks(
                new ViewportPresentationTransactionTestHooks
                {
                    BeforeGroupAsync = (point, _, _) =>
                    {
                        if (point == ViewportPresentationGroupHookPoint.BeforePublish &&
                            Interlocked.Increment(ref publishAttempt) == 2)
                        {
                            throw new InvalidOperationException(
                                "Injected second-candidate publish failure.");
                        }
                        return ValueTask.CompletedTask;
                    },
                    WrapGroupRendered = static (rendered, _) => DelayRenderedAsync(rendered),
                });
            host.Show(desktop, layout.Root, "Viewport Transaction Supersede Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync([control]);

            var origin = layout.First.ActualWidth;
            var extentA = origin + 120;
            var extentB = origin + 240;
            var measurement = control.BeginResizeMeasurement();
            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                resize.RequestCumulative(extentA - origin, isFinal: false);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
                var afterAPublished = layout.Splitter.CapturePresentationTransactionTelemetry();
                if (afterAPublished.UniquePublishedGenerationCount != 1 ||
                    afterAPublished.UniqueRenderedGenerationCount != 0 ||
                    Math.Abs(layout.First.ActualWidth - extentA) > 1.1)
                {
                    throw new InvalidOperationException(
                        "A was not committed while its Rendered barrier remained pending.");
                }

                resize.RequestCumulative(extentB - origin, isFinal: false);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }

            await WaitForTelemetryAsync(
                layout.Splitter,
                metrics => metrics.UniqueRenderedGenerationCount == 1 &&
                           metrics.Outcomes.FaultedCount == 1);
            if (Math.Abs(layout.First.ActualWidth - extentA) > 1.1 ||
                !control.PresentationGeometryMetrics.CurrentSurfaceIsExact)
            {
                throw new InvalidOperationException(
                    "A failed to remain the committed rollback baseline after B failed.");
            }
            var firstAFront = control.CapturePresentationTestSnapshot();
            // A failed candidate posts front-producer recovery at Render priority. Let that
            // recovery boundary finish before beginning the next independent drag transaction.
            await Dispatcher.UIThread.InvokeAsync(
                static () => { },
                DispatcherPriority.Background);

            var secondOrigin = layout.First.ActualWidth;
            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                resize.RequestCumulative(extentB - secondOrigin, isFinal: false);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
                await WaitForTelemetryAsync(
                    layout.Splitter,
                    metrics => metrics.UniqueRenderedGenerationCount == 2);

                // Returning to cumulative delta zero is still a new transaction because B was
                // already committed. Reusing the old A bitmap/generation is forbidden.
                resize.RequestCumulative(0, isFinal: true);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }
            await WaitForTelemetryAsync(
                layout.Splitter,
                metrics => metrics.UniqueRenderedGenerationCount == 3);

            var committedBeforeCancel = control.CapturePresentationTestSnapshot();
            var committedWidthBeforeCancel = layout.First.ActualWidth;
            var prepareEntered = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
            layout.Splitter.ConfigurePresentationTransactionTestHooks(
                new ViewportPresentationTransactionTestHooks
                {
                    BeforeParticipantAsync = async (point, _, token) =>
                    {
                        if (point != ViewportPresentationParticipantHookPoint.BeforePrepare)
                        {
                            return;
                        }
                        prepareEntered.TrySetResult();
                        await Task.Delay(Timeout.InfiniteTimeSpan, token);
                    },
                });
            Task cancelledIdle;
            var cancelledResize = layout.Splitter.BeginAcceptanceResize();
            cancelledResize.RequestCumulative(80, isFinal: false);
            cancelledIdle = cancelledResize.WhenIdleAsync();
            await prepareEntered.Task.WaitAsync(kTimeout);
            // Acceptance disposal uses the same active-request cancellation path as Escape/lost
            // focus, while avoiding synthetic input injection in the process smoke.
            cancelledResize.Dispose();
            await cancelledIdle.WaitAsync(kTimeout);
            await WaitForTelemetryAsync(
                layout.Splitter,
                metrics => metrics.Outcomes.SupersededCount == 1);

            var transactions = layout.Splitter.CapturePresentationTransactionTelemetry();
            var resizeMetrics = control.CaptureResizeMeasurement(measurement);
            var finalAFront = control.CapturePresentationTestSnapshot();
            var rendered = layout.Splitter.PresentationTransactionTelemetry.CaptureEvents()
                .Where(static telemetryEvent =>
                    telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Rendered)
                .ToArray();
            if (rendered.Length != 3 ||
                rendered[0].Extent != rendered[2].Extent ||
                rendered[0].Generation == rendered[2].Generation ||
                rendered[0].TransactionId == rendered[2].TransactionId ||
                ReferenceEquals(firstAFront.VisualSurface, finalAFront.VisualSurface) ||
                transactions.Candidates.ProducedCount != 4 ||
                transactions.Candidates.WasteCount != 1 ||
                transactions.Outcomes.FaultedCount != 1 ||
                transactions.Outcomes.SupersededCount != 1 ||
                transactions.Outcomes.QuarantinedCount != 0 ||
                resizeMetrics.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
                Math.Abs(layout.First.ActualWidth - extentA) > 1.1 ||
                Math.Abs(layout.First.ActualWidth - committedWidthBeforeCancel) > 1.1 ||
                !ReferenceEquals(
                    committedBeforeCancel.VisualSurface,
                    finalAFront.VisualSurface) ||
                !control.PresentationGeometryMetrics.CurrentSurfaceIsExact)
            {
                throw new InvalidOperationException(
                    $"supersede invariants failed: rendered={rendered.Length}, " +
                    $"produced={transactions.Candidates.ProducedCount}, " +
                    $"wasted={transactions.Candidates.WasteCount}, " +
                    $"faulted={transactions.Outcomes.FaultedCount}, " +
                    $"quarantined={transactions.Outcomes.QuarantinedCount}, " +
                    $"hidden={resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}.");
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                "supersede",
                transactions,
                resizeMetrics.RequestedMismatchHiddenDutyCycle);
            if (arguments.Contains("--viewport-transaction-trace", StringComparer.Ordinal))
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "supersede",
                    layout.Splitter.PresentationTransactionTelemetry);
            }
            Console.Out.WriteLine(
                "viewport-transaction-supersede PASS: A Published before Rendered; " +
                "B pre-publish failure retained A; A->B->A used distinct generations; " +
                "active cancellation retained the latest committed front; hidden=0, " +
                "quarantine=0.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            if (observedSplitter is not null)
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "supersede-failure",
                    observedSplitter.PresentationTransactionTelemetry);
            }
            Console.Error.WriteLine($"viewport-transaction-supersede FAIL: {exception}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static async Task DelayRenderedAsync(Task rendered)
    {
        await rendered;
        await Task.Delay(TimeSpan.FromMilliseconds(100));
    }

    private static async Task WaitForTelemetryAsync(
        Editor.Shell.Views.Docking.EditorDockStagedGridSplitter splitter,
        Func<ViewportPresentationTransactionTelemetryMetrics, bool> predicate)
    {
        using var deadline = new CancellationTokenSource(kTimeout);
        while (!predicate(splitter.CapturePresentationTransactionTelemetry()))
        {
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }
}

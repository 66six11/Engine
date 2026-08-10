using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionOverloadSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-transaction-overload";
    private static readonly TimeSpan kTimeout = TimeSpan.FromSeconds(10);

    public static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        var exitCode = 1;
        await using var host = new StudioViewportSmokeHost();
        try
        {
            var options = OverloadOptions.Parse(arguments);
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession("viewport-transaction-overload.scene.json");
            var control = host.CreateControl(session);
            var layout = StudioViewportDockSmokeLayout.Create(control);
            host.Show(desktop, layout.Root, "Viewport Transaction Overload Smoke");
            await StudioViewportSmokeHost.WaitForWarmUpAsync([control]);

            layout.Splitter.ConfigurePresentationTransactionTestHooks(
                new ViewportPresentationTransactionTestHooks
                {
                    BeforeParticipantAsync = (point, _, token) => point ==
                        ViewportPresentationParticipantHookPoint.BeforePrepare
                            ? DelayAsync(options.PrepareDelay, token)
                            : ValueTask.CompletedTask,
                    WrapGroupRendered = (rendered, _) => DelayRenderedAsync(
                        rendered,
                        options.RenderedDelay),
                });
            var originWidth = layout.First.ActualWidth;
            var widths = StudioViewportResizeStimulus.Build(
                "sawtooth",
                options.InputCount,
                originWidth);
            var measurement = control.BeginResizeMeasurement();
            var startedAt = Stopwatch.GetTimestamp();
            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                for (var index = 0; index < widths.Length; index++)
                {
                    resize.RequestCumulative(
                        widths[index] - originWidth,
                        isFinal: index == widths.Length - 1);
                    if (index + 1 < widths.Length)
                    {
                        await StudioViewportResizeStimulus.WaitUntilAsync(
                            startedAt,
                            (index + 1) / options.InputHz);
                    }
                }
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }

            using var deadline = new CancellationTokenSource(kTimeout);
            ViewportResizePresentationMetrics resizeMetrics;
            ViewportPresentationTransactionTelemetryMetrics transactions;
            while (true)
            {
                resizeMetrics = control.CaptureResizeMeasurement(measurement);
                transactions = layout.Splitter.CapturePresentationTransactionTelemetry();
                if (resizeMetrics.FinalGenerationHasExactSurface &&
                    resizeMetrics.FinalGenerationCompleted &&
                    transactions.UniquePublishedGenerationCount ==
                        transactions.UniqueRenderedGenerationCount)
                {
                    break;
                }
                await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
            }

            var scheduling = layout.Splitter.CaptureResizeCoordinatorMetrics();
            if (scheduling.AcceptedRequests != checked((ulong)options.InputCount) ||
                scheduling.ProcessedRequests == 0 ||
                scheduling.AcceptedRequests !=
                    scheduling.ProcessedRequests + scheduling.QueuedSupersededRequests ||
                scheduling.QueuedSupersededRequests == 0 ||
                scheduling.MaximumPendingWork > 2 ||
                scheduling.ActiveCancelledRequests != 0 ||
                scheduling.HasActive || scheduling.HasQueued ||
                transactions.HasOverflowed ||
                transactions.RejectedEventCount != 0 ||
                transactions.UniquePublishedGenerationCount !=
                    transactions.UniqueRenderedGenerationCount ||
                scheduling.ProcessedRequests <
                    checked((ulong)transactions.UniquePublishedGenerationCount) ||
                transactions.Candidates.ProducedCount !=
                    transactions.UniquePublishedGenerationCount ||
                transactions.Candidates.PreparedCandidateCount !=
                    transactions.UniquePublishedGenerationCount ||
                transactions.Candidates.ProducedCount !=
                    transactions.Candidates.PreparedCandidateCount ||
                transactions.Candidates.WasteCount != 0 ||
                transactions.Candidates.WastedCandidateCount != 0 ||
                transactions.Outcomes != default ||
                resizeMetrics.CompletionCoverage < 1 ||
                resizeMetrics.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
                !control.PresentationGeometryMetrics.CurrentSurfaceIsExact)
            {
                throw new InvalidOperationException(
                    $"overload invariants failed: accepted={scheduling.AcceptedRequests}, " +
                    $"processed={scheduling.ProcessedRequests}, " +
                    $"queuedSuperseded={scheduling.QueuedSupersededRequests}, " +
                    $"activeCancelled={scheduling.ActiveCancelledRequests}, " +
                    $"maxPending={scheduling.MaximumPendingWork}, " +
                    $"produced={transactions.Candidates.ProducedCount}, " +
                    $"published={transactions.UniquePublishedGenerationCount}, " +
                    $"rendered={transactions.UniqueRenderedGenerationCount}, " +
                    $"noCandidateProcessed={scheduling.ProcessedRequests - checked((ulong)transactions.UniquePublishedGenerationCount)}, " +
                    $"wasted={transactions.Candidates.WasteCount}, " +
                    $"hidden={resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}.");
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                "overload",
                transactions,
                resizeMetrics.RequestedMismatchHiddenDutyCycle);
            if (arguments.Contains("--viewport-transaction-trace", StringComparer.Ordinal))
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "overload",
                    layout.Splitter.PresentationTransactionTelemetry);
            }
            Console.Out.WriteLine(
                $"viewport-transaction-overload PASS: " +
                $"prepareDelay={options.PrepareDelay.TotalMilliseconds:F0}ms, " +
                $"renderedDelay={options.RenderedDelay.TotalMilliseconds:F0}ms, " +
                $"accepted={scheduling.AcceptedRequests}, " +
                $"processed={scheduling.ProcessedRequests}, " +
                $"queuedSuperseded={scheduling.QueuedSupersededRequests}, " +
                $"activeCancelled={scheduling.ActiveCancelledRequests}, " +
                $"maxPending={scheduling.MaximumPendingWork}, " +
                $"candidateFrames={transactions.Candidates.ProducedCount}, " +
                $"noCandidateProcessed={scheduling.ProcessedRequests - checked((ulong)transactions.UniquePublishedGenerationCount)}, " +
                $"candidateWaste={transactions.Candidates.WasteCount}, " +
                $"uniqueRendered={transactions.UniqueRenderedGenerationCount}.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"viewport-transaction-overload FAIL: {exception.Message}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static ValueTask DelayAsync(TimeSpan delay, CancellationToken token) =>
        delay <= TimeSpan.Zero
            ? ValueTask.CompletedTask
            : new ValueTask(Task.Delay(delay, token));

    private static async Task DelayRenderedAsync(Task rendered, TimeSpan delay)
    {
        await rendered;
        if (delay > TimeSpan.Zero)
        {
            await Task.Delay(delay);
        }
    }

    private sealed record OverloadOptions(
        TimeSpan PrepareDelay,
        TimeSpan RenderedDelay,
        double InputHz,
        int InputCount)
    {
        public static OverloadOptions Parse(string[] arguments)
        {
            var prepare = ParseDouble(Read(arguments, "--viewport-prepare-delay-ms="), 30);
            var rendered = ParseDouble(Read(arguments, "--viewport-rendered-delay-ms="), 30);
            var inputHz = ParseDouble(Read(arguments, "--viewport-input-hz="), 120);
            var inputCount = ParseInt(Read(arguments, "--viewport-input-count="), 120);
            if (prepare is < 0 or > 1000 || rendered is < 0 or > 1000)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(arguments),
                    "Injected delays must be 0..1000 ms.");
            }
            if (inputHz is < 1 or > 1000 || inputCount is < 2 or > 1000)
            {
                throw new ArgumentOutOfRangeException(nameof(arguments));
            }
            return new OverloadOptions(
                TimeSpan.FromMilliseconds(prepare),
                TimeSpan.FromMilliseconds(rendered),
                inputHz,
                inputCount);
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

using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;

namespace Editor.Shell.Composition;

internal static class StudioViewportTransactionFaultSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-transaction-faults";
    private const string kStagePrefix = "--viewport-fault-stage=";
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
            var stage = FaultStage.Parse(arguments);
            var fault = new FaultController(stage);
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession(
                $"viewport-transaction-faults-{stage.Name}.scene.json");
            var control = host.CreateControl(
                session,
                testHooks: new ViewportCompositionControlTestHooks
                {
                    BeforeStageAsync = fault.BeforeEndpointStageAsync,
                    AtSynchronousStage = fault.AtEndpointStage,
                    RejectPreparedValidation = fault.RejectPreparedValidation,
                });
            var layout = StudioViewportDockSmokeLayout.Create(control);
            observedSplitter = layout.Splitter;
            layout.Splitter.ConfigurePresentationTransactionTestHooks(
                new ViewportPresentationTransactionTestHooks
                {
                    BeforeParticipantAsync = fault.BeforeParticipantStageAsync,
                    BeforeGroupAsync = fault.BeforeGroupStageAsync,
                    WrapGroupRendered = fault.WrapRendered,
                });
            host.Show(desktop, layout.Root, $"Viewport Transaction Fault Smoke: {stage.Name}");
            await StudioViewportSmokeHost.WaitForWarmUpAsync([control]);

            var baseline = control.CapturePresentationTestSnapshot();
            var originWidth = layout.First.ActualWidth;
            var targetWidth = originWidth + 120;
            var measurement = control.BeginResizeMeasurement();
            fault.Arm();
            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                resize.RequestCumulative(targetWidth - originWidth, isFinal: true);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }

            if (!fault.Fired && stage.IsRetirementFailure)
            {
                await WaitForAsync(() => fault.Fired);
            }
            if (!fault.Fired)
            {
                throw new InvalidOperationException(
                    $"The requested '{stage.Name}' fault boundary was not reached.");
            }

            if (stage.IsPrePublish)
            {
                await VerifyPrePublishFailureAndRecoveryAsync(
                    stage,
                    control,
                    layout,
                    baseline,
                    originWidth,
                    targetWidth);
            }
            else
            {
                await VerifyPostPublishQuarantineAsync(
                    stage,
                    control,
                    layout,
                    baseline,
                    targetWidth);
            }

            var transactions = layout.Splitter.CapturePresentationTransactionTelemetry();
            var resizeMetrics = control.CaptureResizeMeasurement(measurement);
            var ownership = control.CapturePresentationTestSnapshot();
            if (transactions.HasOverflowed ||
                transactions.RejectedEventCount != 0 ||
                resizeMetrics.RequestedMismatchHiddenDuration != TimeSpan.Zero)
            {
                throw new InvalidOperationException(
                    $"fault telemetry/visibility failed: overflow={transactions.OverflowCount}, " +
                    $"rejected={transactions.RejectedEventCount}, " +
                    $"hidden={resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}.");
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                $"faults-{stage.Name}",
                transactions,
                resizeMetrics.RequestedMismatchHiddenDutyCycle);
            if (arguments.Contains("--viewport-transaction-trace", StringComparer.Ordinal))
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    $"faults-{stage.Name}",
                    layout.Splitter.PresentationTransactionTelemetry);
            }
            Console.Out.WriteLine(
                $"viewport-transaction-faults PASS: stage={stage.Name}, " +
                $"published={transactions.UniquePublishedGenerationCount}, " +
                $"rendered={transactions.UniqueRenderedGenerationCount}, " +
                $"faulted={transactions.Outcomes.FaultedCount}, " +
                $"quarantined={transactions.Outcomes.QuarantinedCount}, " +
                $"candidateWaste={transactions.Candidates.WasteCount}, " +
                $"candidateOwnership=" +
                $"surface:{ownership.CandidateSurfaceCreateAttempts}/" +
                $"{ownership.CandidateSurfacesCreated}," +
                $"stream:{ownership.CandidateStreamOpenAttempts}/" +
                $"{ownership.CandidateStreamsOpened}," +
                $"submit:{ownership.CandidateNativeSubmissions}," +
                $"lease:{ownership.CandidateLeasesAcquired}," +
                $"import:{ownership.CandidateImageImportAttempts}/" +
                $"{ownership.CandidateImagesImported}," +
                $"update:{ownership.CandidateSurfaceUpdateAttempts}," +
                $"cleanup:{ownership.CandidateCleanupCompletions}," +
                $"frameQuarantine:{ownership.QuarantinedFrameCount}, hidden=0.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            if (observedSplitter is not null)
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "faults-failure",
                    observedSplitter.PresentationTransactionTelemetry);
            }
            Console.Error.WriteLine($"viewport-transaction-faults FAIL: {exception}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static async Task VerifyPrePublishFailureAndRecoveryAsync(
        FaultStage stage,
        ViewportCompositionControl control,
        StudioViewportDockSmokeLayout layout,
        ViewportPresentationTestSnapshot baseline,
        double originWidth,
        double targetWidth)
    {
        await WaitForAsync(() =>
        {
            var snapshot = control.CapturePresentationTestSnapshot();
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            return !snapshot.HasPreparingPresentation &&
                   !snapshot.HasPreparedPresentation &&
                   snapshot.RetiringStreamTaskCount == 0 &&
                   snapshot.RetiringSurfaceTaskCount == 0 &&
                   metrics.Outcomes.FaultedCount + metrics.Outcomes.StaleCount +
                       metrics.Outcomes.SupersededCount == 1;
        });

        var failed = control.CapturePresentationTestSnapshot();
        var failedMetrics = layout.Splitter.CapturePresentationTransactionTelemetry();
        var expectedPreparedCandidate = stage.FaultsPreparedCandidate ? 1L : 0L;
        if (failedMetrics.UniquePublishedGenerationCount != 0 ||
            failedMetrics.UniqueRenderedGenerationCount != 0 ||
            failedMetrics.Candidates.ProducedCount != expectedPreparedCandidate ||
            failedMetrics.Candidates.WasteCount != expectedPreparedCandidate ||
            failedMetrics.Outcomes.QuarantinedCount != 0 ||
            (stage.IsValidationFailure
                ? failedMetrics.Outcomes.StaleCount != 1 ||
                  failedMetrics.Outcomes.FaultedCount != 0 ||
                  failedMetrics.Outcomes.SupersededCount != 0
                : stage.IsCancellationFailure
                    ? failedMetrics.Outcomes.SupersededCount != 1 ||
                      failedMetrics.Outcomes.FaultedCount != 0 ||
                      failedMetrics.Outcomes.StaleCount != 0
                : failedMetrics.Outcomes.FaultedCount != 1 ||
                  failedMetrics.Outcomes.StaleCount != 0 ||
                  failedMetrics.Outcomes.SupersededCount != 0) ||
            failed.QuarantinedPresentationCount != 0 ||
            failed.QuarantinedStreamCount != 0 ||
            failed.QuarantinedSurfaceCount != 0 ||
            failed.QuarantinedFrameCount != (stage.IsSubmittedSurfaceFailure ? 1 : 0) ||
            failed.GeometryGeneration != baseline.GeometryGeneration ||
            failed.SurfaceGeneration != baseline.SurfaceGeneration ||
            failed.CurrentExtent != baseline.CurrentExtent ||
            failed.SurfaceExtent != baseline.SurfaceExtent ||
            failed.VisualSize != baseline.VisualSize ||
            Math.Abs(failed.VisualOpacity - 1) > 0.001 ||
            Math.Abs(layout.First.ActualWidth - originWidth) > 1.1)
        {
            throw new InvalidOperationException(
                $"pre-publish rollback failed for {stage.Name}: " +
                $"published={failedMetrics.UniquePublishedGenerationCount}, " +
                $"rendered={failedMetrics.UniqueRenderedGenerationCount}, " +
                $"produced={failedMetrics.Candidates.ProducedCount}, " +
                $"wasted={failedMetrics.Candidates.WasteCount}, " +
                $"faulted={failedMetrics.Outcomes.FaultedCount}, " +
                $"stale={failedMetrics.Outcomes.StaleCount}, " +
                $"quarantined={failedMetrics.Outcomes.QuarantinedCount}, " +
                $"opacity={failed.VisualOpacity:F3}.");
        }
        VerifyFailedCandidateOwnership(stage, baseline, failed);
        var failedFinal = await (layout.Splitter.LatestRetirementCompletion ??
            throw new InvalidOperationException("The failed transaction exposed no final receipt."));
        if (failedFinal.Succeeded ||
            failedFinal.Result == ViewportPresentationTransactionResult.Published)
        {
            throw new InvalidOperationException(
                $"The pre-publish {stage.Name} transaction exposed a successful final receipt.");
        }

        // The fault is one-shot. A fresh transaction to the same exact extent proves that the
        // old front remained usable and all candidate ownership returned before retry.
        await Dispatcher.UIThread.InvokeAsync(static () => { }, DispatcherPriority.Background);
        var retryOrigin = layout.First.ActualWidth;
        using (var resize = layout.Splitter.BeginAcceptanceResize())
        {
            resize.RequestCumulative(targetWidth - retryOrigin, isFinal: true);
            await resize.WhenIdleAsync().WaitAsync(kTimeout);
        }
        await WaitForAsync(() =>
        {
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            return metrics.UniquePublishedGenerationCount == 1 &&
                   metrics.UniqueRenderedGenerationCount == 1 &&
                   control.PresentationGeometryMetrics.CurrentSurfaceIsExact;
        });

        var recovered = control.CapturePresentationTestSnapshot();
        var recoveredMetrics = layout.Splitter.CapturePresentationTransactionTelemetry();
        var recoveredFinal = await (layout.Splitter.LatestRetirementCompletion ??
            throw new InvalidOperationException("The recovery transaction exposed no final receipt."))
            .WaitAsync(kTimeout);
        if (recoveredMetrics.Candidates.ProducedCount != expectedPreparedCandidate + 1 ||
            recoveredMetrics.Candidates.WasteCount != expectedPreparedCandidate ||
            recoveredMetrics.Outcomes.QuarantinedCount != 0 ||
            recovered.HasPreparingPresentation || recovered.HasPreparedPresentation ||
            recovered.QuarantinedPresentationCount != 0 ||
            recovered.QuarantinedStreamCount != 0 ||
            recovered.QuarantinedSurfaceCount != 0 ||
            recovered.QuarantinedFrameCount != (stage.IsSubmittedSurfaceFailure ? 1 : 0) ||
            recovered.GeometryGeneration != baseline.GeometryGeneration + 1 ||
            recovered.SurfaceGeneration != recovered.GeometryGeneration ||
            recovered.SurfaceExtent != recovered.CurrentExtent ||
            Math.Abs(recovered.VisualOpacity - 1) > 0.001 ||
            Math.Abs(layout.First.ActualWidth - targetWidth) > 1.1 ||
            !recoveredFinal.Succeeded)
        {
            throw new InvalidOperationException(
                $"recovery transaction failed for {stage.Name}.");
        }
    }

    private static async Task VerifyPostPublishQuarantineAsync(
        FaultStage stage,
        ViewportCompositionControl control,
        StudioViewportDockSmokeLayout layout,
        ViewportPresentationTestSnapshot baseline,
        double targetWidth)
    {
        await WaitForAsync(() =>
        {
            var snapshot = control.CapturePresentationTestSnapshot();
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            return metrics.UniquePublishedGenerationCount == 1 &&
                   metrics.Outcomes.QuarantinedCount == 1 &&
                   snapshot.QuarantinedPresentationCount == 1 &&
                   snapshot.RetiringStreamTaskCount == 0 &&
                   snapshot.RetiringSurfaceTaskCount == 0;
        });

        var snapshot = control.CapturePresentationTestSnapshot();
        var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
        var finalReport = await (layout.Splitter.LatestRetirementCompletion ??
            throw new InvalidOperationException("The published transaction exposed no final receipt."))
            .WaitAsync(kTimeout);
        var expectedRendered = stage.IsRetirementFailure ? 1 : 0;
        if (metrics.UniqueRenderedGenerationCount != expectedRendered ||
            metrics.Candidates.ProducedCount != 1 ||
            metrics.Candidates.WasteCount != 0 ||
            metrics.Outcomes.FaultedCount != 0 ||
            metrics.Outcomes.StaleCount != 0 ||
            metrics.Outcomes.SupersededCount != 0 ||
            snapshot.QuarantinedStreamCount == 0 ||
            snapshot.QuarantinedSurfaceCount == 0 ||
            snapshot.HasPreparingPresentation || snapshot.HasPreparedPresentation ||
            Math.Abs(snapshot.VisualOpacity - 1) > 0.001 ||
            Math.Abs(layout.First.ActualWidth - targetWidth) > 1.1 ||
            !control.IsDegraded ||
            finalReport.Result != ViewportPresentationTransactionResult.Quarantined)
        {
            throw new InvalidOperationException(
                $"post-publish quarantine failed for {stage.Name}: " +
                $"published={metrics.UniquePublishedGenerationCount}, " +
                $"rendered={metrics.UniqueRenderedGenerationCount}, " +
                $"quarantined={metrics.Outcomes.QuarantinedCount}, " +
                $"resourceQuarantine={snapshot.QuarantinedPresentationCount}/" +
                $"{snapshot.QuarantinedStreamCount}/{snapshot.QuarantinedSurfaceCount}.");
        }

        if (stage.IsBeforeFinalizeFailure)
        {
            if (snapshot.GeometryGeneration != baseline.GeometryGeneration ||
                snapshot.SurfaceGeneration != baseline.SurfaceGeneration)
            {
                throw new InvalidOperationException(
                    "A before-finalize ambiguity advanced committed endpoint geometry.");
            }
        }
        else if (snapshot.GeometryGeneration != baseline.GeometryGeneration + 1 ||
                 snapshot.SurfaceGeneration != snapshot.GeometryGeneration ||
                 snapshot.SurfaceExtent != snapshot.CurrentExtent)
        {
            throw new InvalidOperationException(
                $"The finalized {stage.Name} path lost its committed exact generation.");
        }
    }

    private static void VerifyFailedCandidateOwnership(
        FaultStage stage,
        ViewportPresentationTestSnapshot baseline,
        ViewportPresentationTestSnapshot failed)
    {
        var surfaceAttempts = failed.CandidateSurfaceCreateAttempts -
            baseline.CandidateSurfaceCreateAttempts;
        var surfaces = failed.CandidateSurfacesCreated - baseline.CandidateSurfacesCreated;
        var streamAttempts = failed.CandidateStreamOpenAttempts -
            baseline.CandidateStreamOpenAttempts;
        var streams = failed.CandidateStreamsOpened - baseline.CandidateStreamsOpened;
        var submissions = failed.CandidateNativeSubmissions -
            baseline.CandidateNativeSubmissions;
        var leases = failed.CandidateLeasesAcquired - baseline.CandidateLeasesAcquired;
        var importAttempts = failed.CandidateImageImportAttempts -
            baseline.CandidateImageImportAttempts;
        var images = failed.CandidateImagesImported - baseline.CandidateImagesImported;
        var updates = failed.CandidateSurfaceUpdateAttempts -
            baseline.CandidateSurfaceUpdateAttempts;
        var cleanups = failed.CandidateCleanupCompletions -
            baseline.CandidateCleanupCompletions;
        var expectedSurfaces = stage.EndpointPoint ==
            ViewportCompositionControlTestPoint.BeforeSurfaceCreate ? 0 : 1;
        var expectedStreamAttempts = stage.EndpointPoint ==
            ViewportCompositionControlTestPoint.BeforeSurfaceCreate ? 0 : 1;
        var expectedStreams = stage.EndpointPoint is
            ViewportCompositionControlTestPoint.BeforeSurfaceCreate or
            ViewportCompositionControlTestPoint.BeforeStreamOpen ? 0 : 1;
        var expectedSubmissions = stage.EndpointPoint is
            ViewportCompositionControlTestPoint.BeforeSurfaceCreate or
            ViewportCompositionControlTestPoint.BeforeStreamOpen or
            ViewportCompositionControlTestPoint.BeforeNativeSubmit ? 0 : 1;
        var expectedLeases = stage.EndpointPoint is
            ViewportCompositionControlTestPoint.BeforeSurfaceCreate or
            ViewportCompositionControlTestPoint.BeforeStreamOpen or
            ViewportCompositionControlTestPoint.BeforeNativeSubmit or
            ViewportCompositionControlTestPoint.AfterNativeSubmit ? 0 : 1;
        var expectedImports = stage.EndpointPoint is
            ViewportCompositionControlTestPoint.AfterImageImported or
            ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted
                ? 1
                : stage.FaultsPreparedCandidate || stage.IsValidationFailure
                    ? 1
                    : 0;
        var expectedUpdates = stage.EndpointPoint ==
            ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted ||
            stage.FaultsPreparedCandidate || stage.IsValidationFailure
                ? 1
                : 0;
        var expectedCleanups = expectedSurfaces;
        if (surfaceAttempts != 1 || surfaces != expectedSurfaces ||
            streamAttempts != expectedStreamAttempts || streams != expectedStreams ||
            submissions != expectedSubmissions || leases != expectedLeases ||
            importAttempts != expectedImports || images != expectedImports ||
            updates != expectedUpdates || cleanups != expectedCleanups)
        {
            throw new InvalidOperationException(
                $"candidate ownership accounting failed for {stage.Name}: " +
                $"surface={surfaceAttempts}/{surfaces}, stream={streamAttempts}/{streams}, " +
                $"submit={submissions}, lease={leases}, import={importAttempts}/{images}, " +
                $"update={updates}, cleanup={cleanups}.");
        }
    }

    private static async Task WaitForAsync(Func<bool> predicate)
    {
        using var deadline = new CancellationTokenSource(kTimeout);
        while (!predicate())
        {
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private sealed class FaultController
    {
        private readonly FaultStage stage_;
        private int armed_;
        private int fired_;

        public FaultController(FaultStage stage)
        {
            stage_ = stage;
        }

        public bool Fired => Volatile.Read(ref fired_) != 0;

        public void Arm() => Volatile.Write(ref armed_, 1);

        public ValueTask BeforeEndpointStageAsync(
            ViewportCompositionControlTestPoint point,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (stage_.EndpointPoint == point && TryFire())
            {
                if (stage_.IsCancellationFailure)
                {
                    throw new OperationCanceledException(
                        "Injected cancellation after acquiring the viewport frame lease.",
                        cancellationToken);
                }
                throw Fault();
            }
            return ValueTask.CompletedTask;
        }

        public void AtEndpointStage(ViewportCompositionControlTestPoint point)
        {
            if (stage_.EndpointPoint == point && TryFire())
            {
                throw Fault();
            }
        }

        public bool RejectPreparedValidation() =>
            stage_.IsValidationFailure && TryFire();

        public ValueTask BeforeParticipantStageAsync(
            ViewportPresentationParticipantHookPoint point,
            ViewportPresentationTransactionHookContext _,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (stage_.ParticipantPoint == point && TryFire())
            {
                throw Fault();
            }
            return ValueTask.CompletedTask;
        }

        public ValueTask BeforeGroupStageAsync(
            ViewportPresentationGroupHookPoint point,
            ViewportPresentationTransactionGroupHookContext _,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (stage_.GroupPoint == point && TryFire())
            {
                throw Fault();
            }
            return ValueTask.CompletedTask;
        }

        public Task WrapRendered(
            Task rendered,
            ViewportPresentationTransactionGroupHookContext _) =>
            stage_.IsRenderedFailure && TryFire()
                ? FailAfterAsync(rendered)
                : rendered;

        private bool TryFire() =>
            Volatile.Read(ref armed_) != 0 &&
            Interlocked.CompareExchange(ref fired_, 1, 0) == 0;

        private InvalidOperationException Fault() => new(
            $"Injected viewport transaction fault at '{stage_.Name}'.");

        private async Task FailAfterAsync(Task barrier)
        {
            await barrier;
            throw Fault();
        }
    }

    private sealed record FaultStage(
        string Name,
        ViewportCompositionControlTestPoint? EndpointPoint = null,
        ViewportPresentationParticipantHookPoint? ParticipantPoint = null,
        ViewportPresentationGroupHookPoint? GroupPoint = null,
        bool IsValidationFailure = false,
        bool IsRenderedFailure = false,
        bool IsRetirementFailure = false)
    {
        public bool IsPrePublish =>
            !IsRenderedFailure && !IsRetirementFailure &&
            GroupPoint != ViewportPresentationGroupHookPoint.BeforeFinalize;

        public bool IsBeforeFinalizeFailure =>
            GroupPoint == ViewportPresentationGroupHookPoint.BeforeFinalize;

        public bool FaultsPreparedCandidate =>
            ParticipantPoint == ViewportPresentationParticipantHookPoint.AfterPrepared ||
            GroupPoint == ViewportPresentationGroupHookPoint.BeforePublish ||
            IsValidationFailure;

        public bool IsCancellationFailure =>
            EndpointPoint == ViewportCompositionControlTestPoint.AfterLeaseAcquired;

        public bool IsSubmittedSurfaceFailure =>
            EndpointPoint == ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted;

        public static FaultStage Parse(string[] arguments)
        {
            var value = arguments.FirstOrDefault(argument =>
                argument.StartsWith(kStagePrefix, StringComparison.Ordinal))?[kStagePrefix.Length..]
                ?? "after-prepared";
            return value switch
            {
                "surface-create" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.BeforeSurfaceCreate),
                "stream-open" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.BeforeStreamOpen),
                "native-submit" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.BeforeNativeSubmit),
                "after-submit" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.AfterNativeSubmit),
                "lease" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.AfterLeaseAcquired),
                "image-import" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.AfterImageImported),
                "surface-update" => new(
                    value,
                    EndpointPoint:
                        ViewportCompositionControlTestPoint.AfterSurfaceUpdateSubmitted),
                "after-prepared" => new(
                    value,
                    ParticipantPoint: ViewportPresentationParticipantHookPoint.AfterPrepared),
                "before-publish" => new(
                    value,
                    GroupPoint: ViewportPresentationGroupHookPoint.BeforePublish),
                "validation" => new(value, IsValidationFailure: true),
                "before-finalize" => new(
                    value,
                    GroupPoint: ViewportPresentationGroupHookPoint.BeforeFinalize),
                "rendered" => new(value, IsRenderedFailure: true),
                "retirement" => new(
                    value,
                    EndpointPoint: ViewportCompositionControlTestPoint.BeforeOldSurfaceDispose,
                    IsRetirementFailure: true),
                _ => throw new ArgumentOutOfRangeException(
                    nameof(arguments),
                    value,
                    "Unknown viewport fault stage."),
            };
        }
    }
}

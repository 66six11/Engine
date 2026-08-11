using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls.ApplicationLifetimes;

namespace Editor.Shell.Composition;

internal static class StudioViewportMultiEndpointSmoke
{
    internal const string CommandLineSwitch = "--smoke-viewport-multi-endpoint";
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
            var mode = ParseMode(arguments);
            await host.WarmUpRuntimeAsync();
            var (firstSession, secondSession) = mode is MultiEndpointMode.SceneGame
                ? host.CreateSceneGameSessionPair(
                    "viewport-multi-endpoint.scene.json")
                : host.CreateSceneSessionPair(
                    "viewport-multi-endpoint.scene.json");
            var validationRejected = false;
            var applyFaultInjected = false;
            var rollbackLayoutFaultInjected = false;
            ViewportRenderRequest? firstPublishedRequest = null;
            ViewportRenderRequest? secondPublishedRequest = null;
            Func<bool>? rejectPreparedValidation = null;
            Action<ViewportCompositionControlTestPoint>? atSecondSynchronousStage = null;
            if (mode is MultiEndpointMode.ValidationReject)
            {
                rejectPreparedValidation = () =>
                {
                    validationRejected = true;
                    return true;
                };
            }
            else if (mode is MultiEndpointMode.ApplyMidFault or
                     MultiEndpointMode.RollbackLayoutFault)
            {
                atSecondSynchronousStage = point =>
                {
                    if (!applyFaultInjected &&
                        point == ViewportCompositionControlTestPoint.AfterApplySurface)
                    {
                        applyFaultInjected = true;
                        throw new InvalidOperationException(
                            "Injected second-participant mid-apply failure.");
                    }
                };
            }
            var firstEndpointHooks = new ViewportCompositionControlTestHooks
            {
                RequestPublished = request => firstPublishedRequest = request,
            };
            var secondEndpointHooks = new ViewportCompositionControlTestHooks
            {
                RequestPublished = request => secondPublishedRequest = request,
                RejectPreparedValidation = rejectPreparedValidation,
                AtSynchronousStage = atSecondSynchronousStage,
            };
            var firstControl = host.CreateControl(
                firstSession,
                testHooks: firstEndpointHooks);
            var secondControl = host.CreateControl(
                secondSession,
                testHooks: secondEndpointHooks);
            var layout = StudioViewportDockSmokeLayout.Create(
                firstControl,
                secondControl);
            observedSplitter = layout.Splitter;
            if (mode is MultiEndpointMode.FinalizeFault)
            {
                layout.Splitter.ConfigurePresentationTransactionTestHooks(
                    new ViewportPresentationTransactionTestHooks
                    {
                        BeforeGroupAsync = (point, context, _) =>
                        {
                            if (point == ViewportPresentationGroupHookPoint.BeforeFinalize)
                            {
                                throw new InvalidOperationException(
                                    $"Injected post-publish finalize fault for group " +
                                    $"{context.TransactionId}.");
                            }
                            return ValueTask.CompletedTask;
                        },
                    });
            }
            else if (mode is MultiEndpointMode.RollbackLayoutFault)
            {
                layout.Splitter.ConfigurePresentationTransactionTestHooks(
                    new ViewportPresentationTransactionTestHooks
                    {
                        AtRollback = (point, _) =>
                        {
                            if (point ==
                                ViewportPresentationRollbackHookPoint.BeforeLayoutRollback)
                            {
                                rollbackLayoutFaultInjected = true;
                                throw new InvalidOperationException(
                                    "Injected layout rollback failure.");
                            }
                        },
                    });
            }

            host.Show(
                desktop,
                layout.Root,
                $"Viewport Multi Endpoint Smoke ({ModeName(mode)})");
            await StudioViewportSmokeHost.WaitForWarmUpAsync(
                [firstControl, secondControl]);

            if (firstSession.Current.TargetId != secondSession.Current.TargetId ||
                firstSession.Current.SessionId == secondSession.Current.SessionId)
            {
                throw new InvalidOperationException(
                    "The two viewport endpoints must use distinct sessions for one document.");
            }
            if (mode is MultiEndpointMode.SceneGame &&
                (firstSession.Current.Kind is not ViewportRenderKind.Scene ||
                 secondSession.Current.Kind is not ViewportRenderKind.Game))
            {
                throw new InvalidOperationException(
                    "The mixed endpoint lane lost its distinct Scene and Game session owners.");
            }
            if (firstPublishedRequest is null || secondPublishedRequest is null ||
                firstPublishedRequest.Camera.FieldOfViewAxis is not
                    ViewportFieldOfViewAxis.MaintainHorizontal ||
                MathF.Abs(firstPublishedRequest.Camera.FieldOfViewRadians - (MathF.PI / 2)) >
                    1.0e-6f ||
                (mode is MultiEndpointMode.SceneGame &&
                 (secondPublishedRequest.Camera.FieldOfViewAxis is not
                      ViewportFieldOfViewAxis.MaintainVertical ||
                  MathF.Abs(secondPublishedRequest.Camera.FieldOfViewRadians - (MathF.PI / 3)) >
                      1.0e-6f)) ||
                (mode is not MultiEndpointMode.SceneGame &&
                 (secondPublishedRequest.Camera.FieldOfViewAxis is not
                      ViewportFieldOfViewAxis.MaintainHorizontal ||
                  MathF.Abs(secondPublishedRequest.Camera.FieldOfViewRadians - (MathF.PI / 2)) >
                      1.0e-6f)))
            {
                throw new InvalidOperationException(
                    "The mixed endpoint lane lost its view-local field-of-view policy.");
            }

            if (firstControl.PresentationAtomicScope is not { } firstScope ||
                secondControl.PresentationAtomicScope is not { } secondScope ||
                !ReferenceEquals(firstScope, secondScope))
            {
                throw new InvalidOperationException(
                    "The two viewport endpoints do not share one Avalonia compositor.");
            }

            var firstOrigin = layout.First.ActualWidth;
            var secondOrigin = layout.Second.ActualWidth;
            if (firstOrigin <= 1 || secondOrigin <= 1)
            {
                throw new InvalidOperationException(
                    "The multi-endpoint dock did not receive a renderable initial layout.");
            }
            var delta = Math.Min(120, Math.Max(24, secondOrigin * 0.2));
            var firstGeometryBefore = firstControl.PresentationGeometryMetrics;
            var secondGeometryBefore = secondControl.PresentationGeometryMetrics;
            var firstMeasurement = firstControl.BeginResizeMeasurement();
            var secondMeasurement = secondControl.BeginResizeMeasurement();
            var startedAt = Stopwatch.GetTimestamp();
            Console.Out.WriteLine(
                $"viewport-multi-endpoint phase=resize_begin QPC={startedAt} " +
                $"Frequency={Stopwatch.Frequency} mode={ModeName(mode)} " +
                $"first={firstOrigin:F2} second={secondOrigin:F2} delta={delta:F2}.");

            using (var resize = layout.Splitter.BeginAcceptanceResize())
            {
                resize.RequestCumulative(delta, isFinal: true);
                await resize.WhenIdleAsync().WaitAsync(kTimeout);
            }

            var initialResources = (
                First: firstControl.CapturePresentationTestSnapshot(),
                Second: secondControl.CapturePresentationTestSnapshot());
            RecordResourceSnapshots(
                layout.Splitter.PresentationTransactionTelemetry,
                firstSession.Current.SessionId,
                secondSession.Current.SessionId,
                initialResources.First,
                initialResources.Second);
            await WaitForExpectedOutcomeAsync(layout, mode);
            var resourceSnapshots = await WaitForResourcesAsync(
                firstControl,
                secondControl,
                mode);
            RecordResourceSnapshots(
                layout.Splitter.PresentationTransactionTelemetry,
                firstSession.Current.SessionId,
                secondSession.Current.SessionId,
                resourceSnapshots.First,
                resourceSnapshots.Second);
            RecordReclaimedResources(
                layout.Splitter.PresentationTransactionTelemetry,
                firstSession.Current.SessionId,
                initialResources.First,
                initialResources.Second,
                resourceSnapshots.First,
                resourceSnapshots.Second);
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            var events = layout.Splitter.PresentationTransactionTelemetry.CaptureEvents();
            var firstResize = firstControl.CaptureResizeMeasurement(firstMeasurement);
            var secondResize = secondControl.CaptureResizeMeasurement(secondMeasurement);
            var firstGeometryAfter = firstControl.PresentationGeometryMetrics;
            var secondGeometryAfter = secondControl.PresentationGeometryMetrics;

            switch (mode)
            {
                case MultiEndpointMode.Success:
                case MultiEndpointMode.SceneGame:
                    VerifySuccessfulGroup(
                        firstSession.Current.SessionId,
                        secondSession.Current.SessionId,
                        firstControl,
                        secondControl,
                        firstOrigin,
                        secondOrigin,
                        delta,
                        metrics,
                        events,
                        firstResize,
                        secondResize,
                        firstGeometryAfter,
                        secondGeometryAfter,
                        resourceSnapshots.First,
                        resourceSnapshots.Second);
                    break;
                case MultiEndpointMode.ValidationReject:
                    VerifyValidationRollback(
                        validationRejected,
                        firstControl,
                        secondControl,
                        firstOrigin,
                        secondOrigin,
                        metrics,
                        events,
                        firstResize,
                        secondResize,
                        firstGeometryBefore,
                        secondGeometryBefore,
                        firstGeometryAfter,
                        secondGeometryAfter,
                        resourceSnapshots.First,
                        resourceSnapshots.Second);
                    break;
                case MultiEndpointMode.FinalizeFault:
                    VerifyFinalizeQuarantine(
                        firstControl,
                        secondControl,
                        firstOrigin,
                        secondOrigin,
                        delta,
                        metrics,
                        events,
                        firstResize,
                        secondResize,
                        firstGeometryAfter,
                        secondGeometryAfter,
                        resourceSnapshots.First,
                        resourceSnapshots.Second);
                    break;
                case MultiEndpointMode.ApplyMidFault:
                case MultiEndpointMode.RollbackLayoutFault:
                    VerifyApplyExceptionSafety(
                        mode,
                        applyFaultInjected,
                        rollbackLayoutFaultInjected,
                        firstControl,
                        secondControl,
                        firstOrigin,
                        secondOrigin,
                        metrics,
                        events,
                        resourceSnapshots.First,
                        resourceSnapshots.Second);
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(mode), mode, null);
            }

            StudioViewportTransactionSmokeOutput.WriteSummary(
                $"multi-endpoint-{ModeName(mode)}",
                metrics,
                Math.Max(
                    firstResize.RequestedMismatchHiddenDutyCycle,
                    secondResize.RequestedMismatchHiddenDutyCycle));
            if (arguments.Contains("--viewport-transaction-trace", StringComparer.Ordinal))
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    $"multi-endpoint-{ModeName(mode)}",
                    layout.Splitter.PresentationTransactionTelemetry);
            }
            Console.Out.WriteLine(
                $"viewport-multi-endpoint PASS: mode={ModeName(mode)}, " +
                $"published={metrics.UniquePublishedGenerationCount}, " +
                $"rendered={metrics.UniqueRenderedGenerationCount}, " +
                $"stale={metrics.Outcomes.StaleCount}, " +
                $"quarantined={metrics.Outcomes.QuarantinedCount}, " +
                $"retiring={resourceSnapshots.First.RetiringStreamTaskCount + resourceSnapshots.First.RetiringSurfaceTaskCount + resourceSnapshots.Second.RetiringStreamTaskCount + resourceSnapshots.Second.RetiringSurfaceTaskCount}, " +
                $"hidden={Math.Max(firstResize.RequestedMismatchHiddenDutyCycle, secondResize.RequestedMismatchHiddenDutyCycle):P1}.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            if (observedSplitter is not null)
            {
                StudioViewportTransactionSmokeOutput.WriteEvents(
                    "multi-endpoint-failure",
                    observedSplitter.PresentationTransactionTelemetry);
            }
            Console.Error.WriteLine($"viewport-multi-endpoint FAIL: {exception}");
        }
        finally
        {
            desktop.Shutdown(exitCode);
        }
    }

    private static async Task WaitForExpectedOutcomeAsync(
        StudioViewportDockSmokeLayout layout,
        MultiEndpointMode mode)
    {
        using var deadline = new CancellationTokenSource(kTimeout);
        while (true)
        {
            var metrics = layout.Splitter.CapturePresentationTransactionTelemetry();
            var reached = mode switch
            {
                MultiEndpointMode.Success =>
                    metrics.UniqueRenderedGenerationCount == 2,
                MultiEndpointMode.SceneGame =>
                    metrics.UniqueRenderedGenerationCount == 2,
                MultiEndpointMode.ValidationReject =>
                    metrics.Outcomes.StaleCount == 2 &&
                    metrics.Candidates.WasteCount == 2,
                MultiEndpointMode.FinalizeFault =>
                    metrics.UniquePublishedGenerationCount == 2 &&
                    metrics.Outcomes.QuarantinedCount == 2,
                MultiEndpointMode.ApplyMidFault =>
                    metrics.Outcomes.FaultedCount == 2,
                MultiEndpointMode.RollbackLayoutFault =>
                    metrics.Outcomes.QuarantinedCount == 1 &&
                    metrics.Outcomes.FaultedCount == 1,
                _ => false,
            };
            if (reached)
            {
                return;
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static async Task<(
        ViewportPresentationTestSnapshot First,
        ViewportPresentationTestSnapshot Second)> WaitForResourcesAsync(
        ViewportCompositionControl first,
        ViewportCompositionControl second,
        MultiEndpointMode mode)
    {
        using var deadline = new CancellationTokenSource(kTimeout);
        while (true)
        {
            var firstSnapshot = first.CapturePresentationTestSnapshot();
            var secondSnapshot = second.CapturePresentationTestSnapshot();
            var pending = HasPendingResources(firstSnapshot) ||
                HasPendingResources(secondSnapshot);
            var quarantineReady = mode switch
            {
                MultiEndpointMode.FinalizeFault =>
                    HasPublishedQuarantine(firstSnapshot) &&
                    HasPublishedQuarantine(secondSnapshot),
                MultiEndpointMode.RollbackLayoutFault =>
                    HasPublishedQuarantine(firstSnapshot),
                _ => true,
            };
            if (!pending && quarantineReady)
            {
                return (firstSnapshot, secondSnapshot);
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static bool HasPendingResources(ViewportPresentationTestSnapshot snapshot) =>
        snapshot.HasPreparingPresentation ||
        snapshot.HasPreparedPresentation ||
        snapshot.RetiringStreamTaskCount != 0 ||
        snapshot.RetiringSurfaceTaskCount != 0;

    private static bool HasAnyQuarantine(ViewportPresentationTestSnapshot snapshot) =>
        snapshot.QuarantinedPresentationCount != 0 ||
        snapshot.QuarantinedStreamCount != 0 ||
        snapshot.QuarantinedSurfaceCount != 0 ||
        snapshot.QuarantinedFrameCount != 0;

    private static bool HasPublishedQuarantine(
        ViewportPresentationTestSnapshot snapshot) =>
        snapshot.QuarantinedPresentationCount != 0 &&
        snapshot.QuarantinedStreamCount != 0 &&
        snapshot.QuarantinedSurfaceCount != 0;

    private static void RecordResourceSnapshots(
        ViewportPresentationTransactionTelemetry telemetry,
        ViewportSessionId firstSessionId,
        ViewportSessionId secondSessionId,
        ViewportPresentationTestSnapshot first,
        ViewportPresentationTestSnapshot second)
    {
        var identities = telemetry.CaptureEvents()
            .GroupBy(static item => item.SessionId)
            .ToDictionary(static group => group.Key, static group => group.Last().Identity);
        telemetry.Record(new ViewportPresentationTelemetryEvent(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            Stopwatch.GetTimestamp(),
            identities[firstSessionId],
            TransitionalResourceCount(first)));
        telemetry.Record(new ViewportPresentationTelemetryEvent(
            ViewportPresentationTelemetryEventKind.ResourceSnapshot,
            Stopwatch.GetTimestamp(),
            identities[secondSessionId],
            TransitionalResourceCount(second)));
    }

    private static void RecordReclaimedResources(
        ViewportPresentationTransactionTelemetry telemetry,
        ViewportSessionId identitySessionId,
        ViewportPresentationTestSnapshot initialFirst,
        ViewportPresentationTestSnapshot initialSecond,
        ViewportPresentationTestSnapshot finalFirst,
        ViewportPresentationTestSnapshot finalSecond)
    {
        var initial = checked(
            TransitionalResourceCount(initialFirst) +
            TransitionalResourceCount(initialSecond));
        var final = checked(
            TransitionalResourceCount(finalFirst) +
            TransitionalResourceCount(finalSecond));
        var reclaimed = initial - final;
        if (reclaimed <= 0)
        {
            return;
        }
        var identity = telemetry.CaptureEvents()
            .Last(item => item.SessionId == identitySessionId)
            .Identity;
        telemetry.Record(new ViewportPresentationTelemetryEvent(
            ViewportPresentationTelemetryEventKind.ResourceReclaimed,
            Stopwatch.GetTimestamp(),
            identity,
            reclaimed));
    }

    private static long TransitionalResourceCount(
        ViewportPresentationTestSnapshot snapshot) =>
        (snapshot.HasPreparingPresentation ? 1L : 0) +
        (snapshot.HasPreparedPresentation ? 1L : 0) +
        snapshot.RetiringStreamTaskCount +
        snapshot.RetiringSurfaceTaskCount +
        snapshot.QuarantinedPresentationCount +
        snapshot.QuarantinedStreamCount +
        snapshot.QuarantinedSurfaceCount;

    private static void VerifySuccessfulGroup(
        ViewportSessionId firstSessionId,
        ViewportSessionId secondSessionId,
        ViewportCompositionControl firstControl,
        ViewportCompositionControl secondControl,
        double firstOrigin,
        double secondOrigin,
        double delta,
        ViewportPresentationTransactionTelemetryMetrics metrics,
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportResizePresentationMetrics firstResize,
        ViewportResizePresentationMetrics secondResize,
        ViewportPresentationGeometryMetrics firstGeometry,
        ViewportPresentationGeometryMetrics secondGeometry,
        ViewportPresentationTestSnapshot firstResources,
        ViewportPresentationTestSnapshot secondResources)
    {
        var prepared = Stage(events, ViewportPresentationTelemetryEventKind.Prepared);
        var published = Stage(events, ViewportPresentationTelemetryEventKind.Published);
        var rendered = Stage(events, ViewportPresentationTelemetryEventKind.Rendered);
        VerifyCommonTwoEndpointStages(events, prepared, published);
        if (rendered.Length != 2 ||
            rendered.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            rendered.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            rendered.Select(static item => item.Timestamp).Distinct().Count() != 1 ||
            published[0].TransactionId != rendered[0].TransactionId ||
            metrics.UniquePublishedGenerationCount != 2 ||
            metrics.UniqueRenderedGenerationCount != 2 ||
            metrics.Candidates.ProducedCount != 2 ||
            metrics.Candidates.WasteCount != 0 ||
            metrics.Outcomes != default ||
            !metrics.Resources.EvidenceAvailable ||
            metrics.Resources.CountAtCapture != 0 ||
            metrics.HasOverflowed ||
            metrics.RejectedEventCount != 0 ||
            HasPendingResources(firstResources) ||
            HasPendingResources(secondResources) ||
            HasAnyQuarantine(firstResources) ||
            HasAnyQuarantine(secondResources))
        {
            throw new InvalidOperationException(
                "The successful two-endpoint group did not publish and Render exactly once per endpoint.");
        }

        var firstRendered = rendered.Single(item => item.SessionId == firstSessionId);
        var secondRendered = rendered.Single(item => item.SessionId == secondSessionId);
        VerifyVisibleExactEndpoint(firstControl, firstResize, firstGeometry, firstRendered.Extent);
        VerifyVisibleExactEndpoint(secondControl, secondResize, secondGeometry, secondRendered.Extent);
        VerifySnapshotMatchesPhysicalPanel(firstControl, firstResources, allowQuarantine: false);
        VerifySnapshotMatchesPhysicalPanel(secondControl, secondResources, allowQuarantine: false);
        VerifyCommittedLengths(firstControl, secondControl, firstOrigin, secondOrigin, delta);
    }

    private static void VerifyValidationRollback(
        bool validationRejected,
        ViewportCompositionControl firstControl,
        ViewportCompositionControl secondControl,
        double firstOrigin,
        double secondOrigin,
        ViewportPresentationTransactionTelemetryMetrics metrics,
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportResizePresentationMetrics firstResize,
        ViewportResizePresentationMetrics secondResize,
        ViewportPresentationGeometryMetrics firstBefore,
        ViewportPresentationGeometryMetrics secondBefore,
        ViewportPresentationGeometryMetrics firstAfter,
        ViewportPresentationGeometryMetrics secondAfter,
        ViewportPresentationTestSnapshot firstResources,
        ViewportPresentationTestSnapshot secondResources)
    {
        var prepared = Stage(events, ViewportPresentationTelemetryEventKind.Prepared);
        var published = Stage(events, ViewportPresentationTelemetryEventKind.Published);
        var stale = Stage(events, ViewportPresentationTelemetryEventKind.Stale);
        if (!validationRejected || prepared.Length != 2 || published.Length != 0 ||
            Stage(events, ViewportPresentationTelemetryEventKind.Rendered).Length != 0 ||
            prepared.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            stale.Length != 2 ||
            stale.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            stale.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            stale.Select(static item => item.Timestamp).Distinct().Count() != 1 ||
            stale[0].TransactionId != prepared[0].TransactionId ||
            metrics.UniquePublishedGenerationCount != 0 ||
            metrics.UniqueRenderedGenerationCount != 0 ||
            metrics.Candidates.ProducedCount != 2 ||
            metrics.Candidates.WasteCount != 2 ||
            metrics.Outcomes.StaleCount != 2 ||
            metrics.Outcomes.SupersededCount != 0 ||
            metrics.Outcomes.FaultedCount != 0 ||
            metrics.Outcomes.QuarantinedCount != 0 ||
            !metrics.Resources.EvidenceAvailable ||
            metrics.Resources.CountAtCapture != 0 ||
            metrics.HasOverflowed ||
            metrics.RejectedEventCount != 0 ||
            Math.Abs(firstControl.Bounds.Width - firstOrigin) > 1.1 ||
            Math.Abs(secondControl.Bounds.Width - secondOrigin) > 1.1 ||
            firstBefore.CurrentGeometryGeneration != firstAfter.CurrentGeometryGeneration ||
            firstBefore.SurfaceGeometryGeneration != firstAfter.SurfaceGeometryGeneration ||
            secondBefore.CurrentGeometryGeneration != secondAfter.CurrentGeometryGeneration ||
            secondBefore.SurfaceGeometryGeneration != secondAfter.SurfaceGeometryGeneration ||
            !firstAfter.CurrentSurfaceIsExact ||
            !secondAfter.CurrentSurfaceIsExact ||
            firstResize.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
            secondResize.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
            firstControl.IsDegraded ||
            secondControl.IsDegraded ||
            HasPendingResources(firstResources) ||
            HasPendingResources(secondResources) ||
            HasAnyQuarantine(firstResources) ||
            HasAnyQuarantine(secondResources))
        {
            throw new InvalidOperationException(
                "Second-endpoint validation rejection did not preserve both committed fronts and roll back the whole group.");
        }
        VerifySnapshotMatchesPhysicalPanel(firstControl, firstResources, allowQuarantine: false);
        VerifySnapshotMatchesPhysicalPanel(secondControl, secondResources, allowQuarantine: false);
    }

    private static void VerifyFinalizeQuarantine(
        ViewportCompositionControl firstControl,
        ViewportCompositionControl secondControl,
        double firstOrigin,
        double secondOrigin,
        double delta,
        ViewportPresentationTransactionTelemetryMetrics metrics,
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportResizePresentationMetrics firstResize,
        ViewportResizePresentationMetrics secondResize,
        ViewportPresentationGeometryMetrics firstGeometry,
        ViewportPresentationGeometryMetrics secondGeometry,
        ViewportPresentationTestSnapshot firstResources,
        ViewportPresentationTestSnapshot secondResources)
    {
        var prepared = Stage(events, ViewportPresentationTelemetryEventKind.Prepared);
        var published = Stage(events, ViewportPresentationTelemetryEventKind.Published);
        var quarantined = Stage(events, ViewportPresentationTelemetryEventKind.Quarantined);
        VerifyCommonTwoEndpointStages(events, prepared, published);
        if (Stage(events, ViewportPresentationTelemetryEventKind.Rendered).Length != 0 ||
            quarantined.Length != 2 ||
            quarantined.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            quarantined.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            quarantined.Select(static item => item.Timestamp).Distinct().Count() != 1 ||
            quarantined[0].TransactionId != published[0].TransactionId ||
            metrics.UniquePublishedGenerationCount != 2 ||
            metrics.UniqueRenderedGenerationCount != 0 ||
            metrics.Candidates.ProducedCount != 2 ||
            metrics.Candidates.WasteCount != 0 ||
            metrics.Outcomes.StaleCount != 0 ||
            metrics.Outcomes.SupersededCount != 0 ||
            metrics.Outcomes.FaultedCount != 0 ||
            metrics.Outcomes.QuarantinedCount != 2 ||
            !metrics.Resources.EvidenceAvailable ||
            metrics.Resources.CountAtCapture <= 0 ||
            metrics.HasOverflowed ||
            metrics.RejectedEventCount != 0 ||
            firstResize.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
            secondResize.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
            !firstGeometry.CurrentSurfaceIsExact ||
            !secondGeometry.CurrentSurfaceIsExact ||
            !firstControl.IsDegraded ||
            !secondControl.IsDegraded ||
            HasPendingResources(firstResources) ||
            HasPendingResources(secondResources) ||
            !HasPublishedQuarantine(firstResources) ||
            !HasPublishedQuarantine(secondResources))
        {
            throw new InvalidOperationException(
                "A post-publish finalize fault was not quarantined as one observable two-endpoint group.");
        }
        VerifySnapshotMatchesPhysicalPanel(firstControl, firstResources, allowQuarantine: true);
        VerifySnapshotMatchesPhysicalPanel(secondControl, secondResources, allowQuarantine: true);
        VerifyCommittedLengths(firstControl, secondControl, firstOrigin, secondOrigin, delta);
    }

    private static void VerifyApplyExceptionSafety(
        MultiEndpointMode mode,
        bool applyFaultInjected,
        bool rollbackLayoutFaultInjected,
        ViewportCompositionControl firstControl,
        ViewportCompositionControl secondControl,
        double firstOrigin,
        double secondOrigin,
        ViewportPresentationTransactionTelemetryMetrics metrics,
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportPresentationTestSnapshot firstResources,
        ViewportPresentationTestSnapshot secondResources)
    {
        var rollbackAmbiguous = mode is MultiEndpointMode.RollbackLayoutFault;
        if (!applyFaultInjected ||
            rollbackLayoutFaultInjected != rollbackAmbiguous ||
            Stage(events, ViewportPresentationTelemetryEventKind.Prepared).Length != 2 ||
            Stage(events, ViewportPresentationTelemetryEventKind.Published).Length != 0 ||
            Stage(events, ViewportPresentationTelemetryEventKind.Rendered).Length != 0 ||
            metrics.Outcomes.FaultedCount != (rollbackAmbiguous ? 1 : 2) ||
            metrics.Outcomes.QuarantinedCount != (rollbackAmbiguous ? 1 : 0) ||
            HasPendingResources(firstResources) ||
            HasPendingResources(secondResources) ||
            (rollbackAmbiguous
                ? !HasPublishedQuarantine(firstResources) ||
                  HasAnyQuarantine(secondResources) ||
                  !firstControl.IsDegraded
                : HasAnyQuarantine(firstResources) ||
                  HasAnyQuarantine(secondResources) ||
                  firstControl.IsDegraded ||
                  secondControl.IsDegraded ||
                  Math.Abs(firstControl.Bounds.Width - firstOrigin) > 1.1 ||
                  Math.Abs(secondControl.Bounds.Width - secondOrigin) > 1.1))
        {
            throw new InvalidOperationException(
                rollbackAmbiguous
                    ? "A failed layout rollback did not produce one observable PublicationOutcomeAmbiguous quarantine."
                    : "A second-participant mid-apply failure did not restore both old fronts and layout.");
        }
    }

    private static void VerifySnapshotMatchesPhysicalPanel(
        ViewportCompositionControl control,
        ViewportPresentationTestSnapshot snapshot,
        bool allowQuarantine)
    {
        using var probe = control.BeginPresentationLayoutProbe();
        var hasExactCommittedGeometry = allowQuarantine ||
            panelExtentMatchesCommittedGeometry(snapshot);
        if (!probe.TryGetExactPixelExtent(out var panelExtent) ||
            panelExtent != snapshot.SurfaceExtent ||
            Math.Abs(snapshot.VisualSize.X - panelExtent.Width) > 0.01 ||
            Math.Abs(snapshot.VisualSize.Y - panelExtent.Height) > 0.01 ||
            Math.Abs(snapshot.VisualOpacity - 1) > 0.001 ||
            !hasExactCommittedGeometry ||
            HasPendingResources(snapshot) ||
            (allowQuarantine
                ? !HasPublishedQuarantine(snapshot)
                : HasAnyQuarantine(snapshot)))
        {
            throw new InvalidOperationException(
                $"The endpoint front, composition visual, surface, and physical panel extent diverged: " +
                $"panel={panelExtent.Width}x{panelExtent.Height}, " +
                $"current={snapshot.CurrentExtent.Width}x{snapshot.CurrentExtent.Height}, " +
                $"surface={snapshot.SurfaceExtent.Width}x{snapshot.SurfaceExtent.Height}, " +
                $"visual={snapshot.VisualSize.X:F2}x{snapshot.VisualSize.Y:F2}, " +
                $"generation={snapshot.GeometryGeneration}/{snapshot.SurfaceGeneration}, " +
                $"exact={snapshot.HasExactSurface}, quarantine={HasAnyQuarantine(snapshot)}.");
        }

        static bool panelExtentMatchesCommittedGeometry(
            ViewportPresentationTestSnapshot value) =>
            value.CurrentExtent == value.SurfaceExtent &&
            value.GeometryGeneration == value.SurfaceGeneration &&
            value.HasExactSurface;
    }

    private static void VerifyCommonTwoEndpointStages(
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportPresentationTelemetryEvent[] prepared,
        ViewportPresentationTelemetryEvent[] published)
    {
        var proposed = Stage(events, ViewportPresentationTelemetryEventKind.Proposed);
        if (proposed.Length != 2 || prepared.Length != 2 || published.Length != 2 ||
            proposed.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            prepared.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            published.Select(static item => item.EndpointId).Distinct().Count() != 2 ||
            proposed.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            prepared.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            published.Select(static item => item.TransactionId).Distinct().Count() != 1 ||
            prepared.Max(static item => item.Timestamp) >
                published.Min(static item => item.Timestamp) ||
            published.Select(static item => item.Timestamp).Distinct().Count() != 1)
        {
            throw new InvalidOperationException(
                "The group did not prepare every endpoint before one shared Published boundary.");
        }
    }

    private static void VerifyVisibleExactEndpoint(
        ViewportCompositionControl control,
        ViewportResizePresentationMetrics resize,
        ViewportPresentationGeometryMetrics geometry,
        ViewportExtent renderedExtent)
    {
        if (!resize.FinalGenerationCompleted ||
            !resize.FinalGenerationHasExactSurface ||
            resize.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
            !geometry.CurrentSurfaceIsExact ||
            !geometry.LastPresentationIsExact ||
            geometry.LastPanelExtent != renderedExtent ||
            control.IsDegraded)
        {
            throw new InvalidOperationException(
                $"Endpoint {control.Name ?? "unnamed"} did not retain an exact visible front.");
        }
    }

    private static void VerifyCommittedLengths(
        ViewportCompositionControl firstControl,
        ViewportCompositionControl secondControl,
        double firstOrigin,
        double secondOrigin,
        double delta)
    {
        if (Math.Abs(firstControl.Bounds.Width - (firstOrigin + delta)) > 1.1 ||
            Math.Abs(secondControl.Bounds.Width - (secondOrigin - delta)) > 1.1)
        {
            throw new InvalidOperationException(
                "The committed two-endpoint layout does not match the exact resize proposal.");
        }
    }

    private static ViewportPresentationTelemetryEvent[] Stage(
        System.Collections.Generic.IReadOnlyList<ViewportPresentationTelemetryEvent> events,
        ViewportPresentationTelemetryEventKind kind) =>
        events.Where(item => item.Kind == kind).ToArray();

    private static string ModeName(MultiEndpointMode mode) => mode switch
    {
        MultiEndpointMode.Success => "success",
        MultiEndpointMode.SceneGame => "scene-game",
        MultiEndpointMode.ValidationReject => "validation-reject",
        MultiEndpointMode.FinalizeFault => "finalize-fault",
        MultiEndpointMode.ApplyMidFault => "apply-mid-fault",
        MultiEndpointMode.RollbackLayoutFault => "rollback-layout-fault",
        _ => throw new ArgumentOutOfRangeException(nameof(mode), mode, null),
    };

    private enum MultiEndpointMode
    {
        Success,
        SceneGame,
        ValidationReject,
        FinalizeFault,
        ApplyMidFault,
        RollbackLayoutFault,
    }

    private static MultiEndpointMode ParseMode(string[] arguments)
    {
        const string prefix = "--viewport-multi-mode=";
        var value = arguments.FirstOrDefault(argument =>
            argument.StartsWith(prefix, StringComparison.Ordinal))?[prefix.Length..] ?? "success";
        return value switch
        {
            "success" => MultiEndpointMode.Success,
            "scene-game" => MultiEndpointMode.SceneGame,
            "validation-reject" => MultiEndpointMode.ValidationReject,
            "finalize-fault" => MultiEndpointMode.FinalizeFault,
            "apply-mid-fault" => MultiEndpointMode.ApplyMidFault,
            "rollback-layout-fault" => MultiEndpointMode.RollbackLayoutFault,
            _ => throw new ArgumentException(
                $"Unknown viewport multi-endpoint mode '{value}'.",
                nameof(arguments)),
        };
    }
}

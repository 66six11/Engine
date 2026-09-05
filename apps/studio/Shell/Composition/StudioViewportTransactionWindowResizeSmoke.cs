using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Editor.Shell.Views.Docking;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Composition;

internal static partial class StudioViewportTransactionWindowResizeSmoke
{
    internal const string CommandLineSwitch =
        "--smoke-viewport-transaction-window-resize";
    internal const string EvidenceOptionPrefix = "--viewport-window-evidence=";
    internal const string ObserverReadyEventOptionPrefix =
        "--viewport-window-observer-ready-event=";
    internal const string ReleasePolicyOptionPrefix =
        "--viewport-window-release-policy=";
    private const int DefaultInputCount = 90;
    private const double DefaultInputHz = 120;
    private const uint kWindowMessageSizing = 0x0214;
    private const uint kWindowMessageEnterSizeMove = 0x0231;
    private const uint kWindowMessageExitSizeMove = 0x0232;
    private const int kSizingBottomRight = 8;
    private const double kLayoutEpsilon = 0.01;
    private static readonly TimeSpan kTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan kMaximumCatchUp60HzBudget =
        TimeSpan.FromMilliseconds(1000d / 30d);

    public static async Task RunAsync(
        IClassicDesktopStyleApplicationLifetime desktop,
        string[] arguments)
    {
        var exitCode = 1;
        MainWindow? window = null;
        var timing = new ViewportPreparationTimingRecorder();
        await using var host = new StudioViewportSmokeHost();
        try
        {
            if (!OperatingSystem.IsWindows())
            {
                throw new PlatformNotSupportedException(
                    "The Window resize smoke requires a real Win32 top level.");
            }

            var options = WindowResizeOptions.Parse(arguments);
            await host.WarmUpRuntimeAsync();
            var session = host.CreateSceneSession(
                "viewport-transaction-window-resize.scene.json");
            var projectionRecorder = new ProjectionRequestRecorder();
            var control = host.CreateControl(
                session,
                isRealtime: !options.CapturesProjectionEvidence,
                testHooks: new ViewportCompositionControlTestHooks
                {
                    EnableFlashSentinelCorners = true,
                    PreparationTiming = timing.Record,
                    RequestPublished = options.CapturesProjectionEvidence
                        ? projectionRecorder.ObserveRequest
                        : null,
                    LeaseAcquired = options.CapturesProjectionEvidence
                        ? projectionRecorder.ObserveLease
                        : null,
                });
            var layoutHost = new EditorDockPresentationLayoutHost
            {
                Child = control,
            };
            window = new MainWindow
            {
                Width = 1280,
                Height = 720,
                Title = "Viewport Transaction Window Resize Smoke",
                Content = layoutHost,
            };
            desktop.MainWindow = window;
            window.Show();
            await StudioViewportSmokeHost.WaitForWarmUpAsync(
                [control],
                minimumFrames: options.CapturesProjectionEvidence ? 1UL : 30UL);

            var platformHandle = window.TryGetPlatformHandle();
            if (platformHandle is null ||
                !string.Equals(
                    platformHandle.HandleDescriptor,
                    "HWND",
                    StringComparison.Ordinal) ||
                platformHandle.Handle == 0)
            {
                throw new InvalidOperationException(
                    "The Window resize smoke did not acquire a real HWND.");
            }
            if (!GetWindowRect(platformHandle.Handle, out var initialWindowRect) ||
                !GetClientRect(platformHandle.Handle, out var initialClientRect))
            {
                throw new InvalidOperationException(
                    "The Window resize smoke could not read its initial Win32 geometry.");
            }

            var scaling = window.RenderScaling;
            if (!double.IsFinite(scaling) || scaling <= 0)
            {
                throw new InvalidOperationException(
                    "The Window resize smoke has no valid render scaling.");
            }
            var proposedRects = BuildProposedRects(
                options.Pattern,
                options.InputCount,
                initialWindowRect,
                scaling);
            await WaitForExternalObserverAsync(
                options.ObserverReadyEventName,
                "ready");
            var initialSnapshot = control.CapturePresentationTestSnapshot();
            var measurement = control.BeginResizeMeasurement();
            await using var recorder = options.CollectsContinuousCompositionBatches
                ? new ContinuousCompositionBatchRecorder(
                    window,
                    layoutHost,
                    control,
                    platformHandle.Handle)
                : null;
            if (recorder is not null)
            {
                recorder.Start();
                await recorder.WaitForCountAsync(1, kTimeout);
            }
            WriteHostMetrics("warmup", layoutHost.CaptureMetrics());

            var inputStartedAt = Stopwatch.GetTimestamp();
            var handledSizingMessages = 0;
            var finalRequestBatchBaseline = 0;
            var finalRequestGeometryGenerationBaseline = 0UL;
            var finalRequestSentAt = 0L;
            var finalRequestObservedAt = 0L;
            var exitSizeMoveAt = 0L;
            Size finalRequestedSize = default;
            WriteMarker("resize_begin", inputStartedAt, options);
            _ = SendMessage(
                platformHandle.Handle,
                kWindowMessageEnterSizeMove,
                0,
                0);
            WriteHostMetrics("entered_size_move", layoutHost.CaptureMetrics());
            using (var rectangle = new NativeRectBuffer())
            {
                for (var index = 0; index < proposedRects.Length; index++)
                {
                    var observedBefore = layoutHost.CaptureMetrics().ObservedRequests;
                    if (index == proposedRects.Length - 1)
                    {
                        finalRequestSentAt = Stopwatch.GetTimestamp();
                        finalRequestBatchBaseline = recorder?.Count ?? 0;
                        finalRequestGeometryGenerationBaseline = control
                            .CapturePresentationTestSnapshot()
                            .GeometryGeneration;
                    }
                    rectangle.Write(proposedRects[index]);
                    var result = SendMessage(
                        platformHandle.Handle,
                        kWindowMessageSizing,
                        kSizingBottomRight,
                        rectangle.Pointer);
                    if (result != 0)
                    {
                        handledSizingMessages++;
                    }
                    var requestMetrics = layoutHost.CaptureMetrics();
                    if (index == proposedRects.Length - 1)
                    {
                        if (result != 0)
                        {
                            requestMetrics = await WaitForObservedRequestAsync(
                                layoutHost,
                                observedBefore,
                                kTimeout);
                        }
                        finalRequestObservedAt = Stopwatch.GetTimestamp();
                        finalRequestedSize = requestMetrics.RequestedSize;
                        WriteHostMetrics("final_request_observed", requestMetrics);
                    }
                    if (index + 1 < proposedRects.Length)
                    {
                        await StudioViewportResizeStimulus.WaitUntilAsync(
                            inputStartedAt,
                            (index + 1) / options.InputHz);
                    }
                }
            }
            var inputFinishedAt = Stopwatch.GetTimestamp();
            var pendingRawFinalBeforeExit = false;
            if (options.ReleasePolicy == WindowResizeReleasePolicy.WaitFinal)
            {
                // The regular performance/structural lanes measure the complete requested
                // trajectory. Let the last proposal publish before closing the native sizing
                // interaction so those lanes retain their existing final-target contract.
                await layoutHost.WhenIdleAsync().WaitAsync(kTimeout);
            }
            else
            {
                var pendingMetrics = await WaitForPendingRawFinalProposalAsync(
                    layoutHost,
                    finalRequestedSize,
                    kTimeout);
                pendingRawFinalBeforeExit = true;
                WriteHostMetrics("release_pending_stale", pendingMetrics);
            }

            WriteExternalObserverMarker(
                options.ObserverReadyEventName,
                "release-imminent");
            exitSizeMoveAt = options.CapturesProjectionEvidence
                ? projectionRecorder.CaptureReleaseBoundary()
                : Stopwatch.GetTimestamp();
            _ = SendMessage(
                platformHandle.Handle,
                kWindowMessageExitSizeMove,
                0,
                0);
            WriteHostMetrics("exited_size_move", layoutHost.CaptureMetrics());
            // Do not block before WM_EXITSIZEMOVE in the immediate lane. Waiting there would
            // give the final candidate time to publish and would stop exercising drop-on-exit.
            // The adapter epoch is closed before the observer takes its release baseline.
            await WaitForExternalObserverAsync(
                options.ObserverReadyEventName,
                "release");

            WriteHostMetrics("before_idle", layoutHost.CaptureMetrics());
            await layoutHost.WhenIdleAsync().WaitAsync(kTimeout);
            WriteHostMetrics("after_idle", layoutHost.CaptureMetrics());
            ViewportPresentationTransactionResult? finalRetirementResult = null;
            if (layoutHost.LatestRetirementCompletion is { } retirement)
            {
                var retirementReport = await retirement.WaitAsync(kTimeout);
                finalRetirementResult = retirementReport.Result;
                if (!IsFinalRetirementAccepted(
                        options.ReleasePolicy,
                        retirementReport.Result))
                {
                    throw new InvalidOperationException(
                        $"The final Window resize transaction retired as " +
                        $"{retirementReport.Result}: {retirementReport.Failure}");
                }
            }
            await WaitForEndpointDrainAsync(control, kTimeout);

            var acceptedHostMetrics = layoutHost.CaptureMetrics();
            var finalTruthSize = ResolveFinalTruthSize(
                options.ReleasePolicy,
                finalRequestedSize,
                acceptedHostMetrics.CommittedSize);
            var finalSnapshot = control.CapturePresentationTestSnapshot();
            CompositionBatchSnapshot finalObservation;
            if (recorder is not null)
            {
                finalObservation = await recorder.WaitForAsync(
                    batch => batch.Sequence > finalRequestBatchBaseline &&
                             (options.ReleasePolicy == WindowResizeReleasePolicy.ImmediateExit ||
                              batch.GeometryGeneration >
                                  finalRequestGeometryGenerationBaseline) &&
                             batch.GeometryGeneration == finalSnapshot.GeometryGeneration &&
                             batch.StructurallyExact &&
                             AreClose(batch.HostCommittedSize, finalTruthSize) &&
                             AreClose(batch.HostBoundsSize, finalTruthSize),
                    kTimeout);
                await recorder.StopAsync();
            }
            else
            {
                finalObservation = CaptureCompositionBatchSnapshot(
                    window,
                    layoutHost,
                    control,
                    platformHandle.Handle,
                    sequence: 0);
            }

            var endedAt = Stopwatch.GetTimestamp();
            WriteMarker("resize_end", endedAt, options);
            var batches = recorder?.Capture() ?? [];
            var invalidBatches = batches
                .Where(static batch => !batch.StructurallyExact)
                .ToArray();
            var resizeMetrics = control.CaptureResizeMeasurement(measurement);
            var transactionMetrics = layoutHost.CaptureTransactionTelemetry();
            var hostMetrics = layoutHost.CaptureMetrics();
            var finalCompositionCatchUpBatches = recorder is null
                ? (int?)null
                : checked(finalObservation.Sequence - finalRequestBatchBaseline);
            var transactionEvents = layoutHost.PresentationTransactionTelemetry
                .CaptureEvents();
            var renderedEvents = transactionEvents
                .Where(static telemetryEvent =>
                    telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Rendered)
                .ToArray();
            var finalRenderedEvent = renderedEvents
                .Where(telemetryEvent =>
                    telemetryEvent.Generation == finalObservation.GeometryGeneration &&
                    telemetryEvent.Extent == finalObservation.SurfaceExtent)
                .OrderBy(static telemetryEvent => telemetryEvent.Timestamp)
                .LastOrDefault();
            var renderedPerformance = MeasureUniqueRenderedPerformance(
                transactionEvents,
                inputStartedAt,
                finalRenderedEvent);
            var publishedRenderedCatchUpBatches =
                CountDistinctPublishedRenderedTransactions(
                transactionEvents,
                finalRequestObservedAt,
                finalRenderedEvent.Timestamp);
            var conservativeInputCatchUpBatches =
                CountDistinctPublishedRenderedTransactions(
                transactionEvents,
                finalRequestSentAt,
                finalRenderedEvent.Timestamp);
            var publishedRenderedCatchUpElapsed = publishedRenderedCatchUpBatches > 0 &&
                                                  finalRenderedEvent.IsValid &&
                                                  finalRenderedEvent.Timestamp >=
                                                      finalRequestObservedAt
                ? Stopwatch.GetElapsedTime(
                    finalRequestObservedAt,
                    finalRenderedEvent.Timestamp)
                : TimeSpan.Zero;
            var renderedNonOrigin = renderedEvents
                .Any(telemetryEvent =>
                    telemetryEvent.Extent != initialSnapshot.SurfaceExtent);
            var hasAcceptedFinalWindowRect = GetWindowRect(
                platformHandle.Handle,
                out var finalWindowRect);
            var rawFinalProposalLag = hasAcceptedFinalWindowRect
                ? CalculateRawFinalProposalLag(proposedRects[^1], finalWindowRect)
                : default;
            var rawFinalProposalAccepted = hasAcceptedFinalWindowRect &&
                finalWindowRect == proposedRects[^1];
            var rawFinalProposalDropped = IsRawFinalProposalDropAccepted(
                options.ReleasePolicy,
                pendingRawFinalBeforeExit,
                rawFinalProposalAccepted,
                finalRetirementResult);
            var finalWindowRectMatches = hasAcceptedFinalWindowRect &&
                (options.ReleasePolicy == WindowResizeReleasePolicy.ImmediateExit
                    ? finalObservation.WindowRect == finalWindowRect &&
                      AreClose(finalObservation.HostCommittedSize, finalTruthSize)
                    : finalWindowRect == proposedRects[^1]);
            var projectionEvidence = options.CapturesProjectionEvidence
                ? CaptureProjectionResizeEvidence(
                    projectionRecorder,
                    batches,
                    initialSnapshot,
                    finalSnapshot,
                    finalObservation,
                    initialWindowRect,
                    proposedRects,
                    inputStartedAt,
                    finalRequestSentAt,
                    exitSizeMoveAt,
                    endedAt)
                : null;
            var expectedMinimumRate = options.InputHz >= 60 ? 60 : options.InputHz * 0.95;

            StudioViewportTransactionSmokeOutput.WriteSummary(
                options.StructuredScenario,
                transactionMetrics,
                resizeMetrics.RequestedMismatchHiddenDutyCycle);
            foreach (var batch in batches)
            {
                WriteBatch(batch);
            }

            if (handledSizingMessages != proposedRects.Length ||
                options.CollectsContinuousCompositionBatches && batches.Count == 0 ||
                options.CollectsContinuousCompositionBatches &&
                    invalidBatches.Length != 0 ||
                !finalObservation.StructurallyExact ||
                resizeMetrics.RequestedMismatchHiddenDuration != TimeSpan.Zero ||
                !resizeMetrics.FinalGenerationHasExactSurface ||
                !resizeMetrics.FinalGenerationCompleted ||
                transactionMetrics.HasOverflowed ||
                transactionMetrics.RejectedEventCount != 0 ||
                options.MeasuresRenderedPerformance &&
                    (!renderedPerformance.IsValid ||
                     renderedPerformance.Rate < expectedMinimumRate) ||
                transactionMetrics.Outcomes.FaultedCount != 0 ||
                transactionMetrics.Outcomes.QuarantinedCount != 0 ||
                !IsHostFailureCountAccepted(
                    options.ReleasePolicy,
                    finalRetirementResult,
                    hostMetrics.FailedRequests) ||
                options.ReleasePolicy == WindowResizeReleasePolicy.ImmediateExit &&
                    !rawFinalProposalDropped ||
                hostMetrics.MaximumPendingWork > 2 ||
                hostMetrics.HasActive ||
                hostMetrics.HasQueued ||
                !finalRenderedEvent.IsValid ||
                !IsFinalCatchUpAccepted(
                    options.ReleasePolicy,
                    publishedRenderedCatchUpBatches) ||
                publishedRenderedCatchUpElapsed > kMaximumCatchUp60HzBudget ||
                !finalWindowRectMatches ||
                finalSnapshot.GeometryGeneration <= initialSnapshot.GeometryGeneration ||
                options.IsAbaPattern && !renderedNonOrigin)
            {
                throw new InvalidOperationException(
                    $"Window resize transaction acceptance failed: " +
                    $"evidence={options.Evidence}, pattern={options.Pattern}, " +
                    $"handled={handledSizingMessages}/" +
                    $"{proposedRects.Length}, batches={batches.Count}, " +
                    $"invalidBatches={invalidBatches.Length}, " +
                    $"finalStructurallyExact={finalObservation.StructurallyExact}, " +
                    $"hidden={resizeMetrics.RequestedMismatchHiddenDutyCycle:P1}, " +
                    $"pureUniqueRendered={renderedPerformance.UniqueGenerationCount}, " +
                    $"pureRate={renderedPerformance.Rate:F2}/s, " +
                    $"faulted={transactionMetrics.Outcomes.FaultedCount}, " +
                    $"quarantined={transactionMetrics.Outcomes.QuarantinedCount}, " +
                    $"hostFailed={hostMetrics.FailedRequests}, " +
                    $"pendingRawFinal={pendingRawFinalBeforeExit}, " +
                    $"rawFinalAccepted={rawFinalProposalAccepted}, " +
                    $"rawFinalDropped={rawFinalProposalDropped}, " +
                    $"retirement={finalRetirementResult}, " +
                    $"pendingHighWater={hostMetrics.MaximumPendingWork}, " +
                    $"publishedRenderedCatchUp={publishedRenderedCatchUpBatches}/" +
                    $"{MinimumFinalCatchUp(options.ReleasePolicy)}..2, " +
                    $"inputCatchUp={conservativeInputCatchUpBatches}, " +
                    $"compositionBatches={finalCompositionCatchUpBatches}, " +
                    $"catchUpElapsedMs=" +
                    $"{publishedRenderedCatchUpElapsed.TotalMilliseconds:F2}, " +
                    $"finalWindowRect={finalWindowRectMatches}, " +
                    $"generation={initialSnapshot.GeometryGeneration}->" +
                    $"{finalSnapshot.GeometryGeneration}, " +
                    $"renderedNonOrigin={renderedNonOrigin}.");
            }

            await WaitForExternalObserverAsync(
                options.ObserverReadyEventName,
                "completion",
                finalSnapshot.SurfaceExtent);

            Console.Out.WriteLine(
                "viewport-transaction-window-resize-evidence " +
                JsonSerializer.Serialize(new
                {
                    scenario = options.StructuredScenario,
                    hostKind = "main",
                    pattern = options.Pattern,
                    releasePolicy = ReleasePolicyName(options.ReleasePolicy),
                    evidenceKind = options.EvidenceKind,
                    pixelEvidenceAvailable = false,
                    physicalDisplayedEvidenceAvailable = false,
                    physicalEvidenceReason =
                        "The smoke exercises a real HWND and real Vulkan external surfaces, " +
                        "but it does not capture DWM pixels or correlate a Scene generation " +
                        "with PhysicalDisplayed.",
                    win32 = new
                    {
                        enterSizeMove = 1,
                        sizingRequested = proposedRects.Length,
                        sizingHandled = handledSizingMessages,
                        exitSizeMove = 1,
                        finalWindowRectMatches,
                        rawFinalProposalAccepted,
                        rawFinalProposalDropped,
                        pendingRawFinalBeforeExit,
                        finalRetirementResult = finalRetirementResult?.ToString(),
                        rawFinalProposal = PhysicalRect(proposedRects[^1]),
                        acceptedFinal = hasAcceptedFinalWindowRect
                            ? PhysicalRect(finalWindowRect)
                            : null,
                        rawProposalLagPx = hasAcceptedFinalWindowRect
                            ? new
                            {
                                width = rawFinalProposalLag.Width,
                                height = rawFinalProposalLag.Height,
                            }
                            : null,
                    },
                    input = new
                    {
                        count = options.InputCount,
                        targetHz = options.InputHz,
                        durationMs = Stopwatch.GetElapsedTime(
                            inputStartedAt,
                            inputFinishedAt).TotalMilliseconds,
                    },
                    performanceWindow = options.MeasuresRenderedPerformance
                        ? new
                        {
                            firstProposedQpc = renderedPerformance.FirstProposedTimestamp,
                            finalExactRenderedQpc =
                                renderedPerformance.FinalRenderedTimestamp,
                            durationMs = renderedPerformance.Duration.TotalMilliseconds,
                            uniqueExactRendered =
                                renderedPerformance.UniqueGenerationCount,
                            rate = renderedPerformance.Rate,
                            minimumRate = expectedMinimumRate,
                        }
                        : null,
                    compositionBatches = options.CollectsContinuousCompositionBatches
                        ? new
                        {
                            observed = batches.Count,
                            structurallyExact = batches.Count - invalidBatches.Length,
                            invalid = invalidBatches.Length,
                            blank = batches.Count(static batch => batch.Blank),
                            stretch = batches.Count(static batch => batch.Stretch),
                            crop = batches.Count(static batch => batch.Crop),
                            gap = batches.Count(static batch => batch.Gap),
                            extentMismatch = batches.Count(
                                static batch => batch.ExtentMismatch),
                            clientMismatch = batches.Count(
                                static batch => batch.ClientMismatch),
                        }
                        : null,
                    projection = projectionEvidence is null
                        ? null
                        : new
                        {
                            evidenceAvailable = true,
                            axis = ViewportFieldOfViewAxis.MaintainHorizontal.ToString(),
                            fieldOfViewRadians = projectionEvidence.Requests[0]
                                .Camera.FieldOfViewRadians,
                            targetRevision = projectionEvidence.TargetRevision,
                            fixedWidth = new
                            {
                                proposed = projectionEvidence.ProposedWidthIsFixed,
                                rendered = projectionEvidence.RenderedWidthIsFixed,
                                windowPixels = projectionEvidence.FixedWindowWidth,
                                clientPixels = projectionEvidence.FixedClientWidth,
                                sceneLogical = projectionEvidence.FixedSceneLogicalWidth,
                                surfacePixels = projectionEvidence.FixedSurfaceWidth,
                            },
                            rendered = new
                            {
                                distinctExactHeights =
                                    projectionEvidence.DistinctRenderedHeights,
                                maximumPixelScaleDelta =
                                    projectionEvidence.MaximumPixelScaleDelta,
                                tolerancePixels = 1d,
                                samples = projectionEvidence.Samples.Select(sample => new
                                {
                                    renderedQpc = sample.RenderedTimestamp,
                                    requestSequence = sample.RequestSequence,
                                    targetRevision = sample.TargetRevision,
                                    extent = Extent(sample.Extent),
                                    geometryGeneration = sample.GeometryGeneration,
                                    horizontalFovRadians = sample.HorizontalFovRadians,
                                    derivedVerticalFovRadians = sample.VerticalFovRadians,
                                    xPixelScale = sample.XPixelScale,
                                    yPixelScale = sample.YPixelScale,
                                }),
                            },
                            requests = projectionEvidence.Requests.Select(request => new
                            {
                                timestampQpc = request.Timestamp,
                                session = request.SessionId.Value,
                                sequence = request.Sequence,
                                targetRevision = request.TargetRevision,
                                kind = request.Kind.ToString(),
                                logicalExtent = Extent(request.LogicalExtent),
                                allocationExtent = Extent(request.AllocationExtent),
                                camera = new
                                {
                                    position = Vector(request.Camera.Position),
                                    target = Vector(request.Camera.Target),
                                    up = Vector(request.Camera.Up),
                                    fieldOfViewRadians = request.Camera.FieldOfViewRadians,
                                    fieldOfViewAxis = request.Camera.FieldOfViewAxis.ToString(),
                                    nearPlane = request.Camera.NearPlane,
                                    farPlane = request.Camera.FarPlane,
                                },
                            }),
                            leases = projectionEvidence.Leases.Select(lease => new
                            {
                                timestampQpc = lease.Timestamp,
                                requestSequence = lease.Sequence,
                                targetRevision = lease.TargetRevision,
                                logicalExtent = Extent(lease.LogicalExtent),
                                allocationExtent = Extent(lease.AllocationExtent),
                            }),
                            release = new
                            {
                                inputStartedQpc = projectionEvidence.InputStartedAt,
                                finalRequestSentQpc = projectionEvidence.FinalRequestSentAt,
                                exitSizeMoveQpc = projectionEvidence.ExitSizeMoveAt,
                                endedQpc = projectionEvidence.EndedAt,
                                initialPresentedSequence =
                                    projectionEvidence.InitialRequestSequence,
                                finalProjectionRequestSequence =
                                    projectionEvidence.FinalRequestSequence,
                                finalExactRequestCount =
                                    projectionEvidence.FinalExactRequestCount,
                                postReleaseRequestCount =
                                    projectionEvidence.PostReleaseRequestCount,
                                postReleaseProjectionMutationCount =
                                    projectionEvidence.PostReleaseProjectionMutationCount,
                                cameraIsDefaultScene =
                                    projectionEvidence.CameraIsDefaultScene,
                                finalPresentedSequenceMatches =
                                    projectionEvidence.FinalPresentedSequenceMatches,
                            },
                        },
                    final = new
                    {
                        requested = LogicalSize(finalRequestedSize),
                        accepted = LogicalSize(finalTruthSize),
                        committed = LogicalSize(hostMetrics.CommittedSize),
                        catchUpBatches = publishedRenderedCatchUpBatches,
                        minimumCatchUpBatches = MinimumFinalCatchUp(
                            options.ReleasePolicy),
                        maximumCatchUpBatches = 2,
                        catchUpBasis =
                            "full-identity Published at or after final request observation " +
                            "and Rendered by final exact Rendered",
                        conservativeInputToExactRenderedBatches =
                            conservativeInputCatchUpBatches,
                        continuousCompositionBatches = finalCompositionCatchUpBatches,
                        catchUpElapsedMs =
                            publishedRenderedCatchUpElapsed.TotalMilliseconds,
                        maximumCatchUp60HzBudgetMs =
                            kMaximumCatchUp60HzBudget.TotalMilliseconds,
                        exact = resizeMetrics.FinalGenerationHasExactSurface,
                        rendered = resizeMetrics.FinalGenerationCompleted,
                        structurallyExact = finalObservation.StructurallyExact,
                        rawProposalLag = new
                        {
                            logicalWidth = finalRequestedSize.Width - finalTruthSize.Width,
                            logicalHeight = finalRequestedSize.Height - finalTruthSize.Height,
                        },
                        candidateLag = new
                        {
                            candidateExtent = Extent(finalSnapshot.CandidateExtent),
                            acceptedExtent = Extent(finalSnapshot.SurfaceExtent),
                            outstanding = finalSnapshot.CandidateExtent.Width != 0 ||
                                finalSnapshot.CandidateExtent.Height != 0,
                        },
                    },
                    renderedNonOrigin,
                    visibility = new
                    {
                        hiddenDurationMs =
                            resizeMetrics.RequestedMismatchHiddenDuration.TotalMilliseconds,
                        hiddenDuty = resizeMetrics.RequestedMismatchHiddenDutyCycle,
                    },
                    scaling,
                }));
            var laneSummary = options.MeasuresRenderedPerformance
                ? $"pure unique exact Rendered={renderedPerformance.Rate:F2}/s " +
                  $"({renderedPerformance.UniqueGenerationCount} generations over " +
                  $"{renderedPerformance.Duration.TotalMilliseconds:F2}ms)"
                : $"{batches.Count} continuously sampled composition batches were " +
                  "structurally exact";
            Console.Out.WriteLine(
                $"viewport-transaction-window-resize PASS: " +
                $"evidence={options.Evidence}, pattern={options.Pattern}, " +
                $"release={ReleasePolicyName(options.ReleasePolicy)}, {laneSummary}, " +
                $"post-request Published-to-Rendered catch-up=" +
                $"{publishedRenderedCatchUpBatches}/" +
                $"{MinimumFinalCatchUp(options.ReleasePolicy)}..2, " +
                $"elapsed={publishedRenderedCatchUpElapsed.TotalMilliseconds:F2}ms; " +
                "pixelEvidenceAvailable=false; physicalDisplayedEvidenceAvailable=false.");
            exitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"viewport-transaction-window-resize FAIL: {exception.Message}");
        }
        finally
        {
            if (window is not null)
            {
                window.Content = null;
                window.Close();
            }
            timing.Write();
            desktop.Shutdown(exitCode);
        }
    }

    internal static NativeRect[] BuildProposedRects(
        string pattern,
        int inputCount,
        NativeRect initialRect,
        double renderScaling)
    {
        if (initialRect.Width <= 0 || initialRect.Height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(initialRect));
        }
        if (!double.IsFinite(renderScaling) || renderScaling <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(renderScaling));
        }

        var heightOnly = pattern == "height-aba";
        var logicalOrigin = (heightOnly ? initialRect.Height : initialRect.Width) /
            renderScaling;
        var lengths = StudioViewportResizeStimulus.Build(
            heightOnly ? "aba" : pattern,
            inputCount,
            logicalOrigin,
            renderScaling);
        return lengths.Select(length =>
        {
            var logicalDelta = length - logicalOrigin;
            var primaryPhysicalDelta = checked((int)Math.Round(
                logicalDelta * renderScaling,
                MidpointRounding.AwayFromZero));
            var physicalWidthDelta = heightOnly ? 0 : primaryPhysicalDelta;
            var physicalHeightDelta = heightOnly
                ? checked((int)Math.Round(
                    primaryPhysicalDelta * 0.25,
                    MidpointRounding.AwayFromZero))
                : checked((int)Math.Round(
                    logicalDelta * renderScaling * 0.25,
                    MidpointRounding.AwayFromZero));
            return new NativeRect
            {
                Left = initialRect.Left,
                Top = initialRect.Top,
                Right = checked(initialRect.Right + physicalWidthDelta),
                Bottom = checked(initialRect.Bottom + physicalHeightDelta),
            };
        }).ToArray();
    }

    internal static string? ParseObserverReadyEventName(
        IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        var matches = arguments
            .Where(argument => argument.StartsWith(
                ObserverReadyEventOptionPrefix,
                StringComparison.Ordinal))
            .ToArray();
        if (matches.Length > 1)
        {
            throw new ArgumentException(
                "Window resize observer readiness accepts at most one named event.",
                nameof(arguments));
        }
        if (matches.Length == 0)
        {
            return null;
        }

        var eventName = matches[0][ObserverReadyEventOptionPrefix.Length..];
        if (string.IsNullOrWhiteSpace(eventName))
        {
            throw new ArgumentException(
                "Window resize observer readiness requires a non-empty event name.",
                nameof(arguments));
        }
        return eventName;
    }

    internal static WindowResizeReleasePolicy ParseReleasePolicy(
        IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        var matches = arguments
            .Where(argument => argument.StartsWith(
                ReleasePolicyOptionPrefix,
                StringComparison.Ordinal))
            .ToArray();
        if (matches.Length > 1)
        {
            throw new ArgumentException(
                "Window resize release policy accepts at most one value.",
                nameof(arguments));
        }
        if (matches.Length == 0)
        {
            return WindowResizeReleasePolicy.WaitFinal;
        }

        return matches[0][ReleasePolicyOptionPrefix.Length..] switch
        {
            "wait-final" => WindowResizeReleasePolicy.WaitFinal,
            "immediate-exit" => WindowResizeReleasePolicy.ImmediateExit,
            var value => throw new ArgumentException(
                $"Unknown Window resize release policy '{value}'.",
                nameof(arguments)),
        };
    }

    internal static Size ResolveFinalTruthSize(
        WindowResizeReleasePolicy releasePolicy,
        Size rawFinalRequestedSize,
        Size acceptedCommittedSize) =>
        releasePolicy == WindowResizeReleasePolicy.ImmediateExit
            ? acceptedCommittedSize
            : rawFinalRequestedSize;

    internal static int MinimumFinalCatchUp(WindowResizeReleasePolicy releasePolicy) =>
        releasePolicy == WindowResizeReleasePolicy.ImmediateExit ? 0 : 1;

    internal static bool IsFinalCatchUpAccepted(
        WindowResizeReleasePolicy releasePolicy,
        int catchUpBatches) =>
        catchUpBatches >= MinimumFinalCatchUp(releasePolicy) && catchUpBatches <= 2;

    internal static bool IsFinalRetirementAccepted(
        WindowResizeReleasePolicy releasePolicy,
        ViewportPresentationTransactionResult result) =>
        releasePolicy switch
        {
            WindowResizeReleasePolicy.WaitFinal =>
                result == ViewportPresentationTransactionResult.Committed,
            WindowResizeReleasePolicy.ImmediateExit =>
                result == ViewportPresentationTransactionResult.Cancelled,
            _ => false,
        };

    internal static bool IsHostFailureCountAccepted(
        WindowResizeReleasePolicy releasePolicy,
        ViewportPresentationTransactionResult? finalRetirementResult,
        ulong failedRequests)
    {
        var expectedFailures = releasePolicy == WindowResizeReleasePolicy.ImmediateExit &&
            finalRetirementResult == ViewportPresentationTransactionResult.Cancelled
                ? 1UL
                : 0UL;
        return failedRequests == expectedFailures;
    }

    internal static bool IsRawFinalProposalDropAccepted(
        WindowResizeReleasePolicy releasePolicy,
        bool pendingRawFinalBeforeExit,
        bool rawFinalProposalAccepted,
        ViewportPresentationTransactionResult? finalRetirementResult) =>
        releasePolicy == WindowResizeReleasePolicy.ImmediateExit &&
        pendingRawFinalBeforeExit &&
        !rawFinalProposalAccepted &&
        finalRetirementResult == ViewportPresentationTransactionResult.Cancelled;

    internal static RawFinalProposalLag CalculateRawFinalProposalLag(
        NativeRect rawFinalProposal,
        NativeRect acceptedFinal) =>
        new(
            checked(rawFinalProposal.Width - acceptedFinal.Width),
            checked(rawFinalProposal.Height - acceptedFinal.Height));

    internal static string ReleasePolicyName(WindowResizeReleasePolicy releasePolicy) =>
        releasePolicy switch
        {
            WindowResizeReleasePolicy.WaitFinal => "wait-final",
            WindowResizeReleasePolicy.ImmediateExit => "immediate-exit",
            _ => throw new ArgumentOutOfRangeException(nameof(releasePolicy)),
        };

    private static void WriteExternalObserverMarker(string? eventName, string phase)
    {
        if (eventName is null)
        {
            return;
        }
        WriteObserverHandshake(phase, eventName, expectedSceneExtent: null);
    }

    private static async Task WaitForExternalObserverAsync(
        string? eventName,
        string phase,
        ViewportExtent? expectedSceneExtent = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(phase);
        if (eventName is null)
        {
            return;
        }
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException(
                "Named Window resize observer readiness requires Windows.");
        }

        EventWaitHandle observerReady;
        try
        {
            observerReady = EventWaitHandle.OpenExisting(eventName);
        }
        catch (WaitHandleCannotBeOpenedException exception)
        {
            throw new InvalidOperationException(
                $"The external observer event '{eventName}' does not exist. " +
                "The observer must create and retain it before launching Studio.",
                exception);
        }

        using (observerReady)
        {
            WriteObserverHandshake(
                $"{phase}-waiting",
                eventName,
                expectedSceneExtent);
            var signaled = await Task.Run(() => observerReady.WaitOne(kTimeout));
            if (!signaled)
            {
                throw new TimeoutException(
                    $"The external observer did not signal '{eventName}' within " +
                    $"{kTimeout.TotalMilliseconds:F0} ms.");
            }
            WriteObserverHandshake(
                $"{phase}-signaled",
                eventName,
                expectedSceneExtent);
        }
    }

    private static void WriteObserverHandshake(
        string phase,
        string eventName,
        ViewportExtent? expectedSceneExtent) =>
        Console.Out.WriteLine(
            "viewport-transaction-window-resize-observer " +
            JsonSerializer.Serialize(new
            {
                phase,
                eventName,
                qpc = Stopwatch.GetTimestamp(),
                frequency = Stopwatch.Frequency,
                expectedSceneExtent = expectedSceneExtent is { } extent
                    ? new { width = extent.Width, height = extent.Height }
                    : null,
                timeoutMs = kTimeout.TotalMilliseconds,
            }));

    private static bool AreClose(Size first, Size second) =>
        Math.Abs(first.Width - second.Width) <= kLayoutEpsilon &&
        Math.Abs(first.Height - second.Height) <= kLayoutEpsilon;

    internal static int CountDistinctPublishedRenderedTransactions(
        IEnumerable<ViewportPresentationTelemetryEvent> telemetryEvents,
        long startedAt,
        long completedAt)
    {
        ArgumentNullException.ThrowIfNull(telemetryEvents);
        if (startedAt <= 0 || completedAt < startedAt)
        {
            return 0;
        }

        var events = telemetryEvents.ToArray();
        var publishedAt = events
            .Where(telemetryEvent =>
                telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Published &&
                telemetryEvent.Timestamp >= startedAt &&
                telemetryEvent.Timestamp <= completedAt)
            .GroupBy(static telemetryEvent => telemetryEvent.Identity)
            .ToDictionary(
                static group => group.Key,
                static group => group.Min(telemetryEvent => telemetryEvent.Timestamp));
        return events
            .Where(telemetryEvent =>
                telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Rendered &&
                telemetryEvent.Timestamp <= completedAt &&
                publishedAt.TryGetValue(
                    telemetryEvent.Identity,
                    out var publishedTimestamp) &&
                telemetryEvent.Timestamp >= publishedTimestamp)
            .Select(static telemetryEvent => telemetryEvent.Identity)
            .Distinct()
            .Count();
    }

    internal static WindowResizeRenderedPerformance MeasureUniqueRenderedPerformance(
        IEnumerable<ViewportPresentationTelemetryEvent> telemetryEvents,
        long inputStartedAt,
        ViewportPresentationTelemetryEvent finalRenderedEvent)
    {
        ArgumentNullException.ThrowIfNull(telemetryEvents);
        if (inputStartedAt <= 0 ||
            !finalRenderedEvent.IsValid ||
            finalRenderedEvent.Kind != ViewportPresentationTelemetryEventKind.Rendered ||
            finalRenderedEvent.Timestamp < inputStartedAt)
        {
            return default;
        }

        var scopedEvents = telemetryEvents
            .Where(telemetryEvent =>
                telemetryEvent.EndpointId == finalRenderedEvent.EndpointId &&
                telemetryEvent.SessionId == finalRenderedEvent.SessionId &&
                telemetryEvent.Epoch == finalRenderedEvent.Epoch &&
                telemetryEvent.Timestamp >= inputStartedAt &&
                telemetryEvent.Timestamp <= finalRenderedEvent.Timestamp)
            .ToArray();
        var firstProposedTimestamp = scopedEvents
            .Where(static telemetryEvent =>
                telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Proposed)
            .Select(static telemetryEvent => telemetryEvent.Timestamp)
            .DefaultIfEmpty()
            .Min();
        if (firstProposedTimestamp <= 0 ||
            finalRenderedEvent.Timestamp <= firstProposedTimestamp)
        {
            return default;
        }

        var proposedIdentities = scopedEvents
            .Where(telemetryEvent =>
                telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Proposed &&
                telemetryEvent.Timestamp >= firstProposedTimestamp)
            .Select(static telemetryEvent => telemetryEvent.Identity)
            .ToHashSet();
        var uniqueRenderedGenerationCount = scopedEvents
            .Where(telemetryEvent =>
                telemetryEvent.Kind == ViewportPresentationTelemetryEventKind.Rendered &&
                telemetryEvent.Timestamp >= firstProposedTimestamp &&
                proposedIdentities.Contains(telemetryEvent.Identity))
            .Select(static telemetryEvent => telemetryEvent.Generation)
            .Distinct()
            .Count();
        var duration = Stopwatch.GetElapsedTime(
            firstProposedTimestamp,
            finalRenderedEvent.Timestamp);
        return new WindowResizeRenderedPerformance(
            firstProposedTimestamp,
            finalRenderedEvent.Timestamp,
            duration,
            uniqueRenderedGenerationCount);
    }

    private static async Task<EditorDockPresentationLayoutHostMetrics>
        WaitForObservedRequestAsync(
            EditorDockPresentationLayoutHost host,
            ulong precedingObservedRequests,
            TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        while (true)
        {
            var metrics = host.CaptureMetrics();
            if (metrics.ObservedRequests > precedingObservedRequests)
            {
                return metrics;
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static async Task<EditorDockPresentationLayoutHostMetrics>
        WaitForPendingRawFinalProposalAsync(
            EditorDockPresentationLayoutHost host,
            Size rawFinalProposal,
            TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        while (true)
        {
            var metrics = host.CaptureMetrics();
            if (AreClose(metrics.RequestedSize, rawFinalProposal) &&
                !AreClose(metrics.CommittedSize, rawFinalProposal) &&
                (metrics.HasActive || metrics.HasQueued))
            {
                return metrics;
            }
            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static async Task WaitForEndpointDrainAsync(
        ViewportCompositionControl control,
        TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        while (true)
        {
            var snapshot = control.CapturePresentationTestSnapshot();
            if (!snapshot.HasPreparingPresentation &&
                !snapshot.HasPreparedPresentation &&
                snapshot.RetiringStreamTaskCount == 0 &&
                snapshot.RetiringSurfaceTaskCount == 0 &&
                snapshot.QuarantinedPresentationCount == 0 &&
                snapshot.QuarantinedStreamCount == 0 &&
                snapshot.QuarantinedSurfaceCount == 0 &&
                snapshot.QuarantinedFrameCount == 0)
            {
                return;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
        }
    }

    private static void WriteHostMetrics(
        string phase,
        EditorDockPresentationLayoutHostMetrics metrics) =>
        Console.Out.WriteLine(
            "viewport-transaction-window-resize-host " +
            JsonSerializer.Serialize(new
            {
                phase,
                observed = metrics.ObservedRequests,
                processed = metrics.ProcessedRequests,
                superseded = metrics.QueuedSupersededRequests,
                published = metrics.PublishedRequests,
                failed = metrics.FailedRequests,
                maximumPending = metrics.MaximumPendingWork,
                active = metrics.HasActive,
                queued = metrics.HasQueued,
                committed = LogicalSize(metrics.CommittedSize),
                requested = LogicalSize(metrics.RequestedSize),
            }));

    private static void WriteMarker(
        string phase,
        long timestamp,
        WindowResizeOptions options) =>
        Console.Out.WriteLine(
            $"viewport-transaction-window-resize phase={phase} QPC={timestamp} " +
            $"Frequency={Stopwatch.Frequency} host=main pattern={options.Pattern} " +
            $"inputHz={options.InputHz:F2} count={options.InputCount} " +
            $"evidence={options.Evidence} " +
            $"release={ReleasePolicyName(options.ReleasePolicy)}.");

    private static void WriteBatch(CompositionBatchSnapshot batch)
    {
        Console.Out.WriteLine(
            "viewport-transaction-window-resize-batch " +
            JsonSerializer.Serialize(new
            {
                batch = batch.Sequence,
                renderedQpc = batch.RenderedTimestamp,
                window = new
                {
                    outer = PhysicalRect(batch.WindowRect),
                    client = PhysicalRect(batch.ClientRect),
                    logicalClient = LogicalSize(batch.WindowClientSize),
                },
                host = new
                {
                    bounds = LogicalSize(batch.HostBoundsSize),
                    committed = LogicalSize(batch.HostCommittedSize),
                    requested = LogicalSize(batch.HostRequestedSize),
                },
                scene = new
                {
                    bounds = LogicalSize(batch.SceneBoundsSize),
                    panelExtent = Extent(batch.PanelExtent),
                    visualExtent = Extent(batch.VisualExtent),
                    surfaceExtent = Extent(batch.SurfaceExtent),
                    frontExtent = Extent(batch.FrontExtent),
                    candidateExtent = Extent(batch.CandidateExtent),
                    currentExtent = Extent(batch.CurrentExtent),
                    opacity = batch.Opacity,
                    geometryGeneration = batch.GeometryGeneration,
                    surfaceGeneration = batch.SurfaceGeneration,
                    lastPresentedSequence = batch.LastPresentedSequence,
                    hasSurface = batch.HasSurface,
                    hasExactSurface = batch.HasExactSurface,
                },
                identity = batch.TransactionId is { } transactionId
                    ? new
                    {
                        transaction = (ulong?)transactionId,
                        generation = (ulong?)batch.TransactionGeneration,
                    }
                    : null,
                scaling = batch.RenderScaling,
                blank = batch.Blank,
                stretch = batch.Stretch,
                crop = batch.Crop,
                gap = batch.Gap,
                extentMismatch = batch.ExtentMismatch,
                clientMismatch = batch.ClientMismatch,
                structurallyExact = batch.StructurallyExact,
            }));
    }

    private static object LogicalSize(Size size) => new
    {
        width = size.Width,
        height = size.Height,
    };

    private static object Extent(ViewportExtent extent) => new
    {
        width = extent.Width,
        height = extent.Height,
    };

    private static object PhysicalRect(NativeRect rectangle) => new
    {
        left = rectangle.Left,
        top = rectangle.Top,
        right = rectangle.Right,
        bottom = rectangle.Bottom,
        width = rectangle.Width,
        height = rectangle.Height,
    };

    private static object Vector(Asharia.Runtime.Float3 value) => new
    {
        x = value.X,
        y = value.Y,
        z = value.Z,
    };

    private static ProjectionResizeEvidence CaptureProjectionResizeEvidence(
        ProjectionRequestRecorder recorder,
        IReadOnlyList<CompositionBatchSnapshot> batches,
        ViewportPresentationTestSnapshot initialSnapshot,
        ViewportPresentationTestSnapshot finalSnapshot,
        CompositionBatchSnapshot finalObservation,
        NativeRect initialWindowRect,
        IReadOnlyList<NativeRect> proposedRects,
        long inputStartedAt,
        long finalRequestSentAt,
        long exitSizeMoveAt,
        long endedAt)
    {
        var requests = recorder.CaptureRequests()
            .Where(request => request.Timestamp <= endedAt)
            .OrderBy(static request => request.Timestamp)
            .ToArray();
        var leases = recorder.CaptureLeases()
            .Where(lease => lease.Timestamp <= endedAt)
            .OrderBy(static lease => lease.Timestamp)
            .ToArray();
        var initialRequest = requests.SingleOrDefault(request =>
            request.Sequence == initialSnapshot.LastPresentedSequence) ??
            throw new InvalidOperationException(
                "Projection evidence could not correlate the initial presented request.");
        var baselineCamera = initialRequest.Camera;
        var evidenceRequests = requests
            .Where(request => request.Sequence >= initialRequest.Sequence)
            .ToArray();
        var evidenceLeases = leases
            .Where(lease => lease.Sequence >= initialRequest.Sequence)
            .ToArray();
        var relevantRequests = requests
            .Where(request => request.Timestamp >= inputStartedAt)
            .ToArray();
        var finalExtentRequests = relevantRequests
            .Where(request => request.Timestamp >= finalRequestSentAt &&
                              request.AllocationExtent == finalSnapshot.SurfaceExtent)
            .ToArray();
        var postReleaseRequests = relevantRequests
            .Where(request => request.Timestamp >= exitSizeMoveAt)
            .ToArray();
        var postReleaseProjectionMutations = postReleaseRequests
            .Where(request => request.TargetRevision != initialRequest.TargetRevision ||
                              !SameCamera(request.Camera, baselineCamera))
            .ToArray();
        var exactRenderedBatches = batches
            .Where(batch => batch.RenderedTimestamp >= inputStartedAt &&
                            batch.StructurallyExact &&
                            batch.LastPresentedSequence != 0)
            .ToArray();
        var representativeBatches = exactRenderedBatches
            .GroupBy(static batch => batch.SurfaceExtent.Height)
            .Select(static group => group.Last())
            .OrderBy(static batch => batch.SurfaceExtent.Height)
            .ToArray();
        var fixedSurfaceWidth = initialSnapshot.SurfaceExtent.Width;
        var fixedWindowWidth = initialWindowRect.Width;
        var fixedClientWidth = exactRenderedBatches.Length == 0
            ? 0
            : exactRenderedBatches[0].ClientRect.Width;
        var fixedSceneLogicalWidth = exactRenderedBatches.Length == 0
            ? 0
            : exactRenderedBatches[0].SceneBoundsSize.Width;
        var proposedWidthIsFixed = proposedRects.All(rectangle =>
            rectangle.Left == initialWindowRect.Left &&
            rectangle.Right == initialWindowRect.Right &&
            rectangle.Width == fixedWindowWidth);
        var renderedWidthIsFixed = exactRenderedBatches.All(batch =>
            batch.WindowRect.Width == fixedWindowWidth &&
            batch.ClientRect.Width == fixedClientWidth &&
            Math.Abs(batch.SceneBoundsSize.Width - fixedSceneLogicalWidth) <=
                kLayoutEpsilon &&
            batch.PanelExtent.Width == fixedSurfaceWidth &&
            batch.VisualExtent.Width == fixedSurfaceWidth &&
            batch.SurfaceExtent.Width == fixedSurfaceWidth &&
            batch.FrontExtent.Width == fixedSurfaceWidth &&
            batch.CurrentExtent.Width == fixedSurfaceWidth);
        var cameraIsDefaultScene = SameCamera(
            baselineCamera,
            ViewportCameraSnapshot.DefaultScene);
        var requestsAreExactAndStable = evidenceRequests.All(request =>
            request.Kind == ViewportRenderKind.Scene &&
            request.TargetRevision == initialRequest.TargetRevision &&
            request.TargetRevision != 0 &&
            request.LogicalExtent == request.AllocationExtent &&
            request.AllocationExtent.Width == fixedSurfaceWidth &&
            SameCamera(request.Camera, baselineCamera));
        var requestSequencesAreStrict = evidenceRequests
            .Zip(
                evidenceRequests.Skip(1),
                static (left, right) => left.Sequence < right.Sequence)
            .All(static ordered => ordered);
        if (!cameraIsDefaultScene || !requestsAreExactAndStable ||
            !requestSequencesAreStrict || !proposedWidthIsFixed ||
            !renderedWidthIsFixed || representativeBatches.Length < 2 ||
            finalExtentRequests.Length != 1 || postReleaseRequests.Length != 0)
        {
            throw new InvalidOperationException(
                "Height-only projection evidence lost its fixed-width, camera, request, " +
                "or release invariant.");
        }

        var finalRequest = finalExtentRequests[0];
        if (finalRequest.Timestamp >= exitSizeMoveAt ||
            finalRequest.Sequence != finalSnapshot.LastPresentedSequence ||
            finalRequest.Sequence != finalObservation.LastPresentedSequence ||
            finalRequest.AllocationExtent != finalSnapshot.SurfaceExtent ||
            finalObservation.SurfaceExtent != finalSnapshot.SurfaceExtent)
        {
            throw new InvalidOperationException(
                "The final exact projection request did not remain the presented release truth.");
        }

        var samples = new List<ProjectionScaleSample>(representativeBatches.Length);
        foreach (var batch in representativeBatches)
        {
            var request = requests.SingleOrDefault(candidate =>
                candidate.Sequence == batch.LastPresentedSequence) ??
                throw new InvalidOperationException(
                    $"Rendered projection sequence {batch.LastPresentedSequence} has no request evidence.");
            var lease = leases.LastOrDefault(candidate =>
                candidate.Sequence == batch.LastPresentedSequence) ??
                throw new InvalidOperationException(
                    $"Rendered projection sequence {batch.LastPresentedSequence} has no native lease evidence.");
            if (request.AllocationExtent != batch.SurfaceExtent ||
                request.LogicalExtent != batch.SurfaceExtent ||
                lease.AllocationExtent != batch.SurfaceExtent ||
                lease.LogicalExtent != batch.SurfaceExtent ||
                lease.TargetRevision != request.TargetRevision)
            {
                throw new InvalidOperationException(
                    $"Projection sequence {batch.LastPresentedSequence} lost exact extent/revision correlation.");
            }

            var horizontalFov = request.Camera.FieldOfViewRadians;
            var aspect = request.AllocationExtent.Width /
                (double)request.AllocationExtent.Height;
            var verticalFov = 2d * Math.Atan(
                Math.Tan(horizontalFov / 2d) / aspect);
            var xPixelScale = request.AllocationExtent.Width /
                (2d * Math.Tan(horizontalFov / 2d));
            var yPixelScale = request.AllocationExtent.Height /
                (2d * Math.Tan(verticalFov / 2d));
            samples.Add(new ProjectionScaleSample(
                batch.RenderedTimestamp,
                request.Sequence,
                request.TargetRevision,
                request.AllocationExtent,
                batch.GeometryGeneration,
                horizontalFov,
                verticalFov,
                xPixelScale,
                yPixelScale));
        }

        var baselineScale = samples[0].XPixelScale;
        var maximumScaleDelta = samples.Max(sample => Math.Max(
            Math.Abs(sample.XPixelScale - baselineScale),
            Math.Max(
                Math.Abs(sample.YPixelScale - baselineScale),
                Math.Abs(sample.XPixelScale - sample.YPixelScale))));
        if (maximumScaleDelta > 1d)
        {
            throw new InvalidOperationException(
                $"Fixed-width Scene projection scale drifted by {maximumScaleDelta:F4} px.");
        }

        return new ProjectionResizeEvidence(
            inputStartedAt,
            finalRequestSentAt,
            exitSizeMoveAt,
            endedAt,
            evidenceRequests,
            evidenceLeases,
            samples,
            initialRequest.Sequence,
            finalRequest.Sequence,
            initialRequest.TargetRevision,
            fixedWindowWidth,
            fixedClientWidth,
            fixedSceneLogicalWidth,
            fixedSurfaceWidth,
            representativeBatches.Length,
            finalExtentRequests.Length,
            postReleaseRequests.Length,
            postReleaseProjectionMutations.Length,
            maximumScaleDelta,
            proposedWidthIsFixed,
            renderedWidthIsFixed,
            cameraIsDefaultScene,
            finalSnapshot.LastPresentedSequence == finalRequest.Sequence);

        static bool SameCamera(
            ViewportCameraSnapshot left,
            ViewportCameraSnapshot right) =>
            left.Position == right.Position &&
            left.Target == right.Target &&
            left.Up == right.Up &&
            left.FieldOfViewAxis == right.FieldOfViewAxis &&
            Math.Abs(left.FieldOfViewRadians - right.FieldOfViewRadians) <= 1.0e-6F &&
            Math.Abs(left.NearPlane - right.NearPlane) <= 1.0e-6F &&
            Math.Abs(left.FarPlane - right.FarPlane) <= 1.0e-6F;
    }

    private static CompositionBatchSnapshot CaptureCompositionBatchSnapshot(
        Window window,
        EditorDockPresentationLayoutHost host,
        ViewportCompositionControl control,
        nint windowHandle,
        int sequence)
    {
        Dispatcher.UIThread.VerifyAccess();
        if (!GetWindowRect(windowHandle, out var windowRect) ||
            !GetClientRect(windowHandle, out var clientRect))
        {
            throw new InvalidOperationException(
                "The composition sampler could not read Win32 geometry.");
        }

        var renderScaling = window.RenderScaling;
        var hostMetrics = host.CaptureMetrics();
        var snapshot = control.CapturePresentationTestSnapshot();
        var hasPanelExtent = ViewportPhysicalExtentPolicy.TryCalculate(
            control.Bounds.Width,
            control.Bounds.Height,
            renderScaling,
            out var panelExtent);
        var hasHostExtent = ViewportPhysicalExtentPolicy.TryCalculate(
            host.Bounds.Width,
            host.Bounds.Height,
            renderScaling,
            out var hostExtent);
        var hasVisualExtent = ViewportPhysicalExtentPolicy.TryCalculate(
            snapshot.VisualSize.X,
            snapshot.VisualSize.Y,
            renderScaling,
            out var visualExtent);
        var blank = snapshot.VisualSurface is null ||
                    snapshot.VisualOpacity <= 0.001F ||
                    snapshot.SurfaceExtent.Width == 0 ||
                    snapshot.SurfaceExtent.Height == 0;
        var crop = control.Bounds.Width > host.Bounds.Width + kLayoutEpsilon ||
                   control.Bounds.Height > host.Bounds.Height + kLayoutEpsilon;
        var gap = control.Bounds.Width + kLayoutEpsilon < host.Bounds.Width ||
                  control.Bounds.Height + kLayoutEpsilon < host.Bounds.Height;
        var stretch = !hasVisualExtent || visualExtent != snapshot.SurfaceExtent;
        var extentMismatch = !hasPanelExtent ||
                             panelExtent != visualExtent ||
                             panelExtent != snapshot.SurfaceExtent ||
                             panelExtent != snapshot.FrontExtent ||
                             panelExtent != snapshot.CurrentExtent ||
                             snapshot.GeometryGeneration != snapshot.SurfaceGeneration ||
                             !snapshot.HasExactSurface;
        var clientMismatch = !hasHostExtent ||
                             hostExtent.Width != checked((uint)clientRect.Width) ||
                             hostExtent.Height != checked((uint)clientRect.Height);
        var latestIdentity = host.PresentationTransactionTelemetry
            .CaptureEvents()
            .LastOrDefault(static telemetryEvent =>
                telemetryEvent.Kind is ViewportPresentationTelemetryEventKind.Published or
                    ViewportPresentationTelemetryEventKind.Rendered);
        return new CompositionBatchSnapshot(
            Sequence: sequence,
            RenderedTimestamp: Stopwatch.GetTimestamp(),
            WindowRect: windowRect,
            ClientRect: clientRect,
            WindowClientSize: window.ClientSize,
            HostBoundsSize: host.Bounds.Size,
            HostCommittedSize: hostMetrics.CommittedSize,
            HostRequestedSize: hostMetrics.RequestedSize,
            SceneBoundsSize: control.Bounds.Size,
            RenderScaling: renderScaling,
            PanelExtent: panelExtent,
            VisualExtent: visualExtent,
            SurfaceExtent: snapshot.SurfaceExtent,
            FrontExtent: snapshot.FrontExtent,
            CandidateExtent: snapshot.CandidateExtent,
            CurrentExtent: snapshot.CurrentExtent,
            GeometryGeneration: snapshot.GeometryGeneration,
            SurfaceGeneration: snapshot.SurfaceGeneration,
            LastPresentedSequence: snapshot.LastPresentedSequence,
            Opacity: snapshot.VisualOpacity,
            HasSurface: snapshot.VisualSurface is not null,
            HasExactSurface: snapshot.HasExactSurface,
            TransactionId: latestIdentity.Identity.IsValid
                ? latestIdentity.TransactionId.Value
                : null,
            TransactionGeneration: latestIdentity.Identity.IsValid
                ? latestIdentity.Generation
                : 0,
            Blank: blank,
            Stretch: stretch,
            Crop: crop,
            Gap: gap,
            ExtentMismatch: extentMismatch,
            ClientMismatch: clientMismatch);
    }

    [LibraryImport("user32.dll", EntryPoint = "SendMessageW")]
    private static partial nint SendMessage(
        nint windowHandle,
        uint message,
        nint wParam,
        nint lParam);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetWindowRect(
        nint windowHandle,
        out NativeRect rectangle);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetClientRect(
        nint windowHandle,
        out NativeRect rectangle);

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public readonly int Width => checked(Right - Left);

        public readonly int Height => checked(Bottom - Top);

        public static bool operator ==(NativeRect left, NativeRect right) =>
            left.Left == right.Left && left.Top == right.Top &&
            left.Right == right.Right && left.Bottom == right.Bottom;

        public static bool operator !=(NativeRect left, NativeRect right) => !(left == right);

        public override readonly bool Equals(object? obj) =>
            obj is NativeRect other && this == other;

        public override readonly int GetHashCode() =>
            HashCode.Combine(Left, Top, Right, Bottom);
    }

    private sealed class NativeRectBuffer : IDisposable
    {
        private readonly nint pointer_ = Marshal.AllocHGlobal(
            Marshal.SizeOf<NativeRect>());

        public nint Pointer => pointer_;

        public void Write(NativeRect rectangle) =>
            Marshal.StructureToPtr(rectangle, pointer_, fDeleteOld: false);

        public void Dispose() => Marshal.FreeHGlobal(pointer_);
    }

    private sealed class ProjectionRequestRecorder
    {
        private readonly object gate_ = new();
        private readonly List<ProjectionRequestObservation> requests_ = [];
        private readonly List<ProjectionLeaseObservation> leases_ = [];

        public void ObserveRequest(ViewportRenderRequest request)
        {
            ArgumentNullException.ThrowIfNull(request);
            var observation = new ProjectionRequestObservation(
                Stopwatch.GetTimestamp(),
                request.SessionId,
                request.Sequence,
                request.TargetRevision,
                request.Kind,
                request.LogicalExtent,
                request.AllocationExtent,
                request.Camera);
            lock (gate_)
            {
                requests_.Add(observation);
            }
        }

        public void ObserveLease(ViewportFrameLease lease)
        {
            ArgumentNullException.ThrowIfNull(lease);
            var observation = new ProjectionLeaseObservation(
                Stopwatch.GetTimestamp(),
                lease.RequestSequence,
                lease.TargetRevision,
                lease.LogicalExtent,
                lease.AllocationExtent);
            lock (gate_)
            {
                leases_.Add(observation);
            }
        }

        public IReadOnlyList<ProjectionRequestObservation> CaptureRequests()
        {
            lock (gate_)
            {
                return requests_.ToArray();
            }
        }

        public IReadOnlyList<ProjectionLeaseObservation> CaptureLeases()
        {
            lock (gate_)
            {
                return leases_.ToArray();
            }
        }

        public long CaptureReleaseBoundary()
        {
            lock (gate_)
            {
                var latestRequestTimestamp = requests_.Count == 0
                    ? 0
                    : requests_[^1].Timestamp;
                var boundary = Stopwatch.GetTimestamp();
                while (boundary <= latestRequestTimestamp)
                {
                    boundary = Stopwatch.GetTimestamp();
                }
                return boundary;
            }
        }
    }

    private sealed record ProjectionRequestObservation(
        long Timestamp,
        ViewportSessionId SessionId,
        ulong Sequence,
        ulong TargetRevision,
        ViewportRenderKind Kind,
        ViewportExtent LogicalExtent,
        ViewportExtent AllocationExtent,
        ViewportCameraSnapshot Camera);

    private sealed record ProjectionLeaseObservation(
        long Timestamp,
        ulong Sequence,
        ulong TargetRevision,
        ViewportExtent LogicalExtent,
        ViewportExtent AllocationExtent);

    private sealed record ProjectionScaleSample(
        long RenderedTimestamp,
        ulong RequestSequence,
        ulong TargetRevision,
        ViewportExtent Extent,
        ulong GeometryGeneration,
        double HorizontalFovRadians,
        double VerticalFovRadians,
        double XPixelScale,
        double YPixelScale);

    private sealed record ProjectionResizeEvidence(
        long InputStartedAt,
        long FinalRequestSentAt,
        long ExitSizeMoveAt,
        long EndedAt,
        IReadOnlyList<ProjectionRequestObservation> Requests,
        IReadOnlyList<ProjectionLeaseObservation> Leases,
        IReadOnlyList<ProjectionScaleSample> Samples,
        ulong InitialRequestSequence,
        ulong FinalRequestSequence,
        ulong TargetRevision,
        int FixedWindowWidth,
        int FixedClientWidth,
        double FixedSceneLogicalWidth,
        uint FixedSurfaceWidth,
        int DistinctRenderedHeights,
        int FinalExactRequestCount,
        int PostReleaseRequestCount,
        int PostReleaseProjectionMutationCount,
        double MaximumPixelScaleDelta,
        bool ProposedWidthIsFixed,
        bool RenderedWidthIsFixed,
        bool CameraIsDefaultScene,
        bool FinalPresentedSequenceMatches);

    private sealed class ContinuousCompositionBatchRecorder : IAsyncDisposable
    {
        private readonly object gate_ = new();
        private readonly Window window_;
        private readonly EditorDockPresentationLayoutHost host_;
        private readonly ViewportCompositionControl control_;
        private readonly nint windowHandle_;
        private readonly CancellationTokenSource cancellation_ = new();
        private readonly List<CompositionBatchSnapshot> batches_ = [];
        private Task? observation_;

        public ContinuousCompositionBatchRecorder(
            Window window,
            EditorDockPresentationLayoutHost host,
            ViewportCompositionControl control,
            nint windowHandle)
        {
            window_ = window ?? throw new ArgumentNullException(nameof(window));
            host_ = host ?? throw new ArgumentNullException(nameof(host));
            control_ = control ?? throw new ArgumentNullException(nameof(control));
            windowHandle_ = windowHandle;
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

        public void Start()
        {
            Dispatcher.UIThread.VerifyAccess();
            if (observation_ is not null)
            {
                throw new InvalidOperationException(
                    "The composition batch recorder is already running.");
            }
            observation_ = ObserveAsync(cancellation_.Token);
        }

        public async Task StopAsync()
        {
            if (observation_ is null)
            {
                return;
            }
            cancellation_.Cancel();
            try
            {
                await observation_;
            }
            catch (OperationCanceledException) when (cancellation_.IsCancellationRequested)
            {
            }
        }

        public async ValueTask DisposeAsync()
        {
            await StopAsync();
            cancellation_.Dispose();
        }

        public async Task WaitForCountAsync(int expectedCount, TimeSpan timeout)
        {
            using var deadline = new CancellationTokenSource(timeout);
            while (Count < expectedCount)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
            }
        }

        public async Task<CompositionBatchSnapshot> WaitForAsync(
            Func<CompositionBatchSnapshot, bool> predicate,
            TimeSpan timeout)
        {
            ArgumentNullException.ThrowIfNull(predicate);
            using var deadline = new CancellationTokenSource(timeout);
            while (true)
            {
                lock (gate_)
                {
                    var match = batches_.FirstOrDefault(predicate);
                    if (match is not null)
                    {
                        return match;
                    }
                }
                await Task.Delay(TimeSpan.FromMilliseconds(1), deadline.Token);
            }
        }

        public IReadOnlyList<CompositionBatchSnapshot> Capture()
        {
            lock (gate_)
            {
                return batches_.ToArray();
            }
        }

        private async Task ObserveAsync(CancellationToken cancellationToken)
        {
            var rendered = RequestRenderedBatch();
            while (true)
            {
                await rendered.WaitAsync(cancellationToken);
                cancellationToken.ThrowIfCancellationRequested();
                Dispatcher.UIThread.VerifyAccess();
                // Arm the next observation before snapshot work so the UI continuation leaves
                // the smallest possible gap between sampled compositor batches. This remains
                // structural sampling, not a claim that every DWM-displayed frame was captured.
                var nextRendered = RequestRenderedBatch();
                var snapshot = CaptureOnUiThread();
                lock (gate_)
                {
                    batches_.Add(snapshot with { Sequence = batches_.Count + 1 });
                }
                rendered = nextRendered;
            }
        }

        private Task RequestRenderedBatch()
        {
            var visual = ElementComposition.GetElementVisual(control_) ??
                throw new InvalidOperationException(
                    "The Scene composition visual is unavailable.");
            return visual.Compositor.RequestCompositionBatchCommitAsync().Rendered;
        }

        private CompositionBatchSnapshot CaptureOnUiThread() =>
            CaptureCompositionBatchSnapshot(
                window_,
                host_,
                control_,
                windowHandle_,
                sequence: 0);
    }

    private sealed record CompositionBatchSnapshot(
        int Sequence,
        long RenderedTimestamp,
        NativeRect WindowRect,
        NativeRect ClientRect,
        Size WindowClientSize,
        Size HostBoundsSize,
        Size HostCommittedSize,
        Size HostRequestedSize,
        Size SceneBoundsSize,
        double RenderScaling,
        ViewportExtent PanelExtent,
        ViewportExtent VisualExtent,
        ViewportExtent SurfaceExtent,
        ViewportExtent FrontExtent,
        ViewportExtent CandidateExtent,
        ViewportExtent CurrentExtent,
        ulong GeometryGeneration,
        ulong SurfaceGeneration,
        ulong LastPresentedSequence,
        float Opacity,
        bool HasSurface,
        bool HasExactSurface,
        ulong? TransactionId,
        ulong TransactionGeneration,
        bool Blank,
        bool Stretch,
        bool Crop,
        bool Gap,
        bool ExtentMismatch,
        bool ClientMismatch)
    {
        public bool StructurallyExact =>
            !Blank && !Stretch && !Crop && !Gap && !ExtentMismatch && !ClientMismatch;
    }

    internal readonly record struct WindowResizeRenderedPerformance(
        long FirstProposedTimestamp,
        long FinalRenderedTimestamp,
        TimeSpan Duration,
        int UniqueGenerationCount)
    {
        public bool IsValid =>
            FirstProposedTimestamp > 0 &&
            FinalRenderedTimestamp > FirstProposedTimestamp &&
            Duration > TimeSpan.Zero &&
            UniqueGenerationCount > 0;

        public double Rate => IsValid
            ? UniqueGenerationCount / Duration.TotalSeconds
            : 0;
    }

    internal enum WindowResizeReleasePolicy
    {
        WaitFinal,
        ImmediateExit,
    }

    internal readonly record struct RawFinalProposalLag(int Width, int Height);

    private sealed record WindowResizeOptions(
        string Pattern,
        double InputHz,
        int InputCount,
        string Evidence,
        string? ObserverReadyEventName,
        WindowResizeReleasePolicy ReleasePolicy)
    {
        public bool MeasuresRenderedPerformance => Evidence == "performance";

        public bool CollectsContinuousCompositionBatches => Evidence == "continuous";

        public bool CapturesProjectionEvidence => Pattern == "height-aba";

        public bool IsAbaPattern => Pattern is "aba" or "height-aba";

        public string StructuredScenario => CapturesProjectionEvidence
            ? "window-resize-projection"
            : MeasuresRenderedPerformance
                ? "window-resize-performance"
                : "window-resize-structural";

        public string EvidenceKind => CapturesProjectionEvidence
            ? "scene-horizontal-fov-height-resize"
            : MeasuresRenderedPerformance
                ? "transaction-rendered-performance"
                : "continuous-composition-batch-structural";

        public static WindowResizeOptions Parse(string[] arguments)
        {
            var pattern = Read(arguments, "--viewport-window-pattern=") ?? "aba";
            if (pattern is not ("grow" or "shrink" or "aba" or "height-aba"))
            {
                throw new ArgumentException(
                    $"Unknown Window resize pattern '{pattern}'.",
                    nameof(arguments));
            }
            var inputHz = ParseDouble(
                Read(arguments, "--viewport-window-input-hz="),
                DefaultInputHz);
            var inputCount = ParseInt(
                Read(arguments, "--viewport-window-input-count="),
                DefaultInputCount);
            var evidence = Read(arguments, EvidenceOptionPrefix) ?? "performance";
            var observerReadyEventName = ParseObserverReadyEventName(arguments);
            var releasePolicy = ParseReleasePolicy(arguments);
            if (evidence is not ("performance" or "continuous"))
            {
                throw new ArgumentException(
                    $"Unknown Window resize evidence lane '{evidence}'.",
                    nameof(arguments));
            }
            if (pattern == "height-aba" && evidence != "continuous")
            {
                throw new ArgumentException(
                    "Height-only projection evidence requires continuous composition sampling.",
                    nameof(arguments));
            }
            if (!double.IsFinite(inputHz) || inputHz < 1 || inputHz > 1000)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(arguments),
                    "Window resize input Hz must be 1..1000.");
            }
            if (inputCount < 2 || inputCount > 240)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(arguments),
                    "Window resize input count must be 2..240.");
            }
            return new WindowResizeOptions(
                pattern,
                inputHz,
                inputCount,
                evidence,
                observerReadyEventName,
                releasePolicy);
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

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Avalonia.Threading;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public readonly record struct ViewportPresentationParticipant(
    string ParticipantId,
    ViewportCompositionControl Endpoint,
    ViewportExtent TargetExtent);

public sealed record ViewportPresentationTransactionRequest(
    ulong TransactionId,
    IReadOnlyList<ViewportPresentationParticipant> Participants,
    long RequestedAtTimestamp);

public readonly record struct ViewportPresentationTransactionReport(
    ulong TransactionId,
    ViewportPresentationTransactionResult Result,
    int ParticipantCount,
    long RequestedAtTimestamp,
    long PreparedAtTimestamp,
    long PublishedAtTimestamp,
    long RenderedAtTimestamp,
    long VisibleCommitAtTimestamp,
    string? Failure)
{
    public bool Succeeded => Result == ViewportPresentationTransactionResult.Committed;
}

public sealed class ViewportPresentationTransactionExecution
{
    internal ViewportPresentationTransactionExecution(
        bool published,
        ViewportPresentationTransactionReport publication,
        Task<ViewportPresentationTransactionReport> completion,
        Task<ViewportPresentationTransactionReport> retirementCompletion)
    {
        Published = published;
        Publication = publication;
        Completion = completion;
        RetirementCompletion = retirementCompletion;
    }

    public bool Published { get; }

    public ViewportPresentationTransactionReport Publication { get; }

    public Task<ViewportPresentationTransactionReport> Completion { get; }

    /// <summary>
    /// Completes only after the replaced front resources have retired, or after their ownership
    /// has moved to an observable quarantine. It does not delay the Rendered completion/KPI.
    /// </summary>
    public Task<ViewportPresentationTransactionReport> RetirementCompletion { get; }
}

/// <summary>
/// Coordinates an all-or-nothing visible publish for viewport endpoints owned by one Avalonia
/// compositor. Rendering and surface import finish before layout changes; layout validation and
/// visual swaps then happen in one UI turn and share one composition batch barrier.
/// </summary>
public sealed class ViewportPresentationTransactionCoordinator
{
    private Task publishBarrier_ = Task.CompletedTask;
    private readonly SemaphoreSlim publishTurn_ = new(1, 1);
    private readonly ViewportPresentationTransactionTelemetry? telemetry_;
    private readonly ViewportPresentationTransactionTestHooks? testHooks_;

    public ViewportPresentationTransactionCoordinator()
    {
    }

    internal ViewportPresentationTransactionCoordinator(
        ViewportPresentationTransactionTelemetry telemetry,
        ViewportPresentationTransactionTestHooks? testHooks = null)
    {
        ArgumentNullException.ThrowIfNull(telemetry);
        telemetry_ = telemetry;
        testHooks_ = testHooks;
    }

    public async Task<ViewportPresentationTransactionExecution> ExecuteAsync(
        ViewportPresentationTransactionRequest request,
        Action applyLayout,
        Action rollbackLayout,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(applyLayout);
        ArgumentNullException.ThrowIfNull(rollbackLayout);
        Dispatcher.UIThread.VerifyAccess();

        ValidateRequest(request);
        var runtime = CreateRuntimeState(request);
        var prepared = new List<PreparedParticipant>(request.Participants.Count);
        var proposed = new List<ProposedParticipant>(request.Participants.Count);
        var receipts = new List<PublishedParticipant>(request.Participants.Count);
        var layoutApplied = false;
        var groupPublished = false;
        var preparedAt = 0L;
        var publishedAt = 0L;
        var renderedAt = 0L;
        ViewportPresentationTransactionGroupHookContext? groupHookContext = null;

        try
        {
            object? atomicScope = null;
            for (var index = 0; index < request.Participants.Count; index++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var participant = request.Participants[index];
                var participantId = ParticipantId(index);
                RequireApplied(runtime.BeginPreparing(participantId));
                var transactionId = new ViewportPresentationTransactionId(
                    request.TransactionId);
                var proposedIdentity = participant.Endpoint.CreatePresentationTelemetryIdentity(
                    transactionId,
                    participant.Endpoint.NextPresentationGeometryGeneration,
                    participant.TargetExtent);
                proposed.Add(new ProposedParticipant(participant, participantId, proposedIdentity));
                var hookContext = new ViewportPresentationTransactionHookContext(
                    request.TransactionId,
                    index,
                    participant.ParticipantId,
                    proposedIdentity);
                RecordTelemetry(
                    ViewportPresentationTelemetryEventKind.Proposed,
                    request.RequestedAtTimestamp,
                    proposedIdentity);

                var scope = participant.Endpoint.PresentationAtomicScope;
                if (scope is null || atomicScope is not null && !ReferenceEquals(scope, atomicScope))
                {
                    throw new InvalidOperationException(
                        "A viewport presentation transaction is atomic only within one compositor.");
                }

                atomicScope ??= scope;

                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeParticipantStageAsync(
                        ViewportPresentationParticipantHookPoint.BeforePrepare,
                        hookContext,
                        cancellationToken);
                }

                var handle = await participant.Endpoint.PreparePresentationAsync(
                    participant.TargetExtent,
                    cancellationToken);
                var preparedIdentity = participant.Endpoint.CreatePresentationTelemetryIdentity(
                    transactionId,
                    handle.Ticket.CandidateGeometryGeneration,
                    participant.TargetExtent);
                prepared.Add(new PreparedParticipant(
                    participant,
                    participantId,
                    handle,
                    preparedIdentity,
                    hookContext with { Identity = preparedIdentity }));
                var participantPreparedAt = Stopwatch.GetTimestamp();
                RecordTelemetry(
                    ViewportPresentationTelemetryEventKind.Prepared,
                    participantPreparedAt,
                    preparedIdentity);
                RecordTelemetry(
                    ViewportPresentationTelemetryEventKind.CandidateProduced,
                    participantPreparedAt,
                    preparedIdentity,
                    amount: checked((long)handle.CandidateRenderedFrames));
                RequireApplied(runtime.MarkPrepared(participantId));
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeParticipantStageAsync(
                        ViewportPresentationParticipantHookPoint.AfterPrepared,
                        hookContext with { Identity = preparedIdentity },
                        cancellationToken);
                }
            }

            preparedAt = Stopwatch.GetTimestamp();
            cancellationToken.ThrowIfCancellationRequested();
            foreach (var participant in prepared)
            {
                if (!participant.Participant.Endpoint.ArmPreparedPresentation(participant.Handle))
                {
                    throw new InvalidOperationException(
                        $"Viewport participant '{participant.Participant.ParticipantId}' became stale before layout commit.");
                }
            }

            groupHookContext = new ViewportPresentationTransactionGroupHookContext(
                request.TransactionId,
                prepared.Select(static participant => participant.HookContext).ToArray());
            await publishTurn_.WaitAsync(cancellationToken);
            try
            {
                await publishBarrier_.WaitAsync(cancellationToken);
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeGroupStageAsync(
                        ViewportPresentationGroupHookPoint.BeforePublish,
                        groupHookContext!,
                        cancellationToken);
                }

                layoutApplied = true;
                applyLayout();
                cancellationToken.ThrowIfCancellationRequested();
                foreach (var participant in prepared)
                {
                    if (!participant.Participant.Endpoint.TryValidatePreparedPresentation(
                            participant.Handle))
                    {
                        throw new ViewportPresentationExtentMismatchException(
                            participant.Participant.ParticipantId);
                    }

                    RequireApplied(runtime.Validate(participant.StateId));
                }

                foreach (var participant in prepared)
                {
                    try
                    {
                        var receipt = participant.Participant.Endpoint.ApplyPreparedPresentation(
                            participant.Handle,
                            ViewportGeometryChangeSource.Bounds);
                        receipts.Add(new PublishedParticipant(participant, receipt));
                    }
                    catch (ViewportCompositionControl.PublicationOutcomeAmbiguousException exception)
                    {
                        receipts.Add(new PublishedParticipant(participant, exception.Receipt));
                        throw;
                    }
                }

                var rendered = prepared[0].Participant.Endpoint.RequestPresentationBatchRendered();
                if (testHooks_ is not null)
                {
                    rendered = testHooks_.WrapRenderedTask(rendered, groupHookContext!);
                }

                publishBarrier_ = rendered;
                RequireApplied(runtime.Publish());
                groupPublished = true;
                publishedAt = Stopwatch.GetTimestamp();
                foreach (var participant in receipts)
                {
                    RecordTelemetry(
                        ViewportPresentationTelemetryEventKind.Published,
                        publishedAt,
                        participant.Prepared.Identity);
                }

                var retirements = new Task[receipts.Count];
                if (testHooks_ is not null)
                {
                    await testHooks_.BeforeGroupStageAsync(
                        ViewportPresentationGroupHookPoint.BeforeFinalize,
                        groupHookContext!,
                        cancellationToken);
                }

                for (var index = 0; index < receipts.Count; index++)
                {
                    var participant = receipts[index];
                    retirements[index] = participant.Prepared.Participant.Endpoint
                        .FinalizePreparedPresentation(participant.Receipt, rendered);
                }

                var publication = Report(
                    request,
                    ViewportPresentationTransactionResult.Published,
                    preparedAt,
                    publishedAt,
                    renderedAt: 0,
                    completedAt: publishedAt,
                    failure: null);
                var retirementCompletion = new TaskCompletionSource<
                    ViewportPresentationTransactionReport>(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                var completion = CompletePublishedTransactionAsync(
                    request,
                    runtime,
                    prepared,
                    receipts,
                    retirements,
                    rendered,
                    preparedAt,
                    publishedAt,
                    retirementCompletion);
                return new ViewportPresentationTransactionExecution(
                    true,
                    publication,
                    completion,
                    retirementCompletion.Task);
            }
            finally
            {
                publishTurn_.Release();
            }
        }
        catch (OperationCanceledException) when (!groupPublished)
        {
            return CompletePrePublishFailure(
                request,
                runtime,
                proposed,
                prepared,
                receipts,
                layoutApplied,
                rollbackLayout,
                groupHookContext,
                ViewportPresentationTelemetryEventKind.Superseded,
                ViewportPresentationTransactionResult.Cancelled,
                ViewportPresentationFailureCode.Cancelled,
                ViewportPresentationFailureDisposition.Recoverable,
                preparedAt,
                publishedAt,
                renderedAt,
                "The presentation proposal was superseded or cancelled.");
        }
        catch (ViewportPresentationExtentMismatchException exception)
        {
            return CompletePrePublishFailure(
                request,
                runtime,
                proposed,
                prepared,
                receipts,
                layoutApplied,
                rollbackLayout,
                groupHookContext,
                ViewportPresentationTelemetryEventKind.Stale,
                ViewportPresentationTransactionResult.ExtentMismatch,
                ViewportPresentationFailureCode.ValidationRejected,
                ViewportPresentationFailureDisposition.Recoverable,
                preparedAt,
                publishedAt,
                renderedAt,
                exception.Message);
        }
        catch (ViewportPresentationRecoverableException exception)
        {
            return CompletePrePublishFailure(
                request,
                runtime,
                proposed,
                prepared,
                receipts,
                layoutApplied,
                rollbackLayout,
                groupHookContext,
                ViewportPresentationTelemetryEventKind.Faulted,
                ViewportPresentationTransactionResult.RecoverableFailure,
                ViewportPresentationFailureCode.Backpressure,
                ViewportPresentationFailureDisposition.Recoverable,
                preparedAt,
                publishedAt,
                renderedAt,
                exception.Message);
        }
        catch (Exception exception) when (!groupPublished)
        {
            return CompletePrePublishFailure(
                request,
                runtime,
                proposed,
                prepared,
                receipts,
                layoutApplied,
                rollbackLayout,
                groupHookContext,
                ViewportPresentationTelemetryEventKind.Faulted,
                ViewportPresentationTransactionResult.Invalidated,
                ViewportPresentationFailureCode.PreparationRejected,
                ViewportPresentationFailureDisposition.Terminal,
                preparedAt,
                publishedAt,
                renderedAt,
                exception.Message);
        }
        catch (Exception exception)
        {
            RecordOutcome(
                receipts.Select(static receipt => receipt.Prepared.Identity),
                ViewportPresentationTelemetryEventKind.Quarantined);
            runtime.Quarantine(new ViewportPresentationFailure(
                ViewportPresentationFailureCode.RenderOutcomeAmbiguous,
                ViewportPresentationFailureDisposition.Ambiguous));
            QuarantinePublishedParticipants(receipts, exception.Message);
            return PublishedFailureExecution(Report(
                request,
                ViewportPresentationTransactionResult.Quarantined,
                preparedAt,
                publishedAt,
                renderedAt,
                Stopwatch.GetTimestamp(),
                exception.Message));
        }
    }

    private static void ValidateRequest(ViewportPresentationTransactionRequest request)
    {
        if (request.TransactionId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(request), "TransactionId must be non-zero.");
        }

        if (request.Participants is null || request.Participants.Count == 0)
        {
            throw new ArgumentException(
                "A viewport presentation transaction requires at least one participant.",
                nameof(request));
        }

        var participantIds = new HashSet<string>(StringComparer.Ordinal);
        var endpoints = new HashSet<ViewportCompositionControl>();
        foreach (var participant in request.Participants)
        {
            if (string.IsNullOrWhiteSpace(participant.ParticipantId) ||
                !participantIds.Add(participant.ParticipantId))
            {
                throw new ArgumentException(
                    "Viewport participant identities must be non-empty and unique.",
                    nameof(request));
            }

            ArgumentNullException.ThrowIfNull(participant.Endpoint);
            if (!endpoints.Add(participant.Endpoint))
            {
                throw new ArgumentException(
                    "A presentation endpoint may participate only once per transaction.",
                    nameof(request));
            }

            if (participant.TargetExtent.Width == 0 || participant.TargetExtent.Height == 0)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(request),
                    "Viewport presentation target extents must be renderable.");
            }
        }
    }

    private static ViewportPresentationTransactionState CreateRuntimeState(
        ViewportPresentationTransactionRequest request)
    {
        var scope = new ViewportPresentationAtomicScopeId(1);
        var proposals = request.Participants
            .Select((_, index) => new ViewportPresentationParticipantProposal(
                ParticipantId(index),
                scope))
            .ToArray();
        var creation = ViewportPresentationTransactionState.Create(
            new ViewportPresentationTransactionId(request.TransactionId),
            proposals);
        return creation.State ?? throw new InvalidOperationException(
            "The viewport presentation transaction state rejected a validated request.");
    }

    private static ViewportPresentationParticipantId ParticipantId(int index) =>
        new(checked((ulong)index + 1));

    private static void RequireApplied(ViewportPresentationTransitionResult transition)
    {
        if (transition.Disposition is not (
            ViewportPresentationTransitionDisposition.Applied or
            ViewportPresentationTransitionDisposition.AlreadyApplied))
        {
            throw new InvalidOperationException(
                $"Invalid viewport presentation transition at phase {transition.Phase}.");
        }
    }

    private ViewportPresentationTransactionExecution CompletePrePublishFailure(
        ViewportPresentationTransactionRequest request,
        ViewportPresentationTransactionState runtime,
        IReadOnlyList<ProposedParticipant> proposed,
        IReadOnlyList<PreparedParticipant> prepared,
        IReadOnlyList<PublishedParticipant> receipts,
        bool layoutApplied,
        Action rollbackLayout,
        ViewportPresentationTransactionGroupHookContext? groupHookContext,
        ViewportPresentationTelemetryEventKind outcome,
        ViewportPresentationTransactionResult result,
        ViewportPresentationFailureCode failureCode,
        ViewportPresentationFailureDisposition failureDisposition,
        long preparedAt,
        long publishedAt,
        long renderedAt,
        string failure)
    {
        var rollback = RollbackBeforePublish(
            receipts,
            prepared,
            layoutApplied,
            rollbackLayout,
            groupHookContext);
        if (rollback.IsAmbiguous)
        {
            var ambiguousParticipants = rollback.AmbiguousPublished
                .Select(static participant => participant.Prepared.StateId)
                .ToHashSet();
            var timestamp = Stopwatch.GetTimestamp();
            foreach (var participant in proposed)
            {
                RecordTelemetry(
                    ambiguousParticipants.Contains(participant.StateId)
                        ? ViewportPresentationTelemetryEventKind.Quarantined
                        : outcome,
                    timestamp,
                    participant.Identity);
            }
            foreach (var participant in prepared)
            {
                if (!ambiguousParticipants.Contains(participant.StateId))
                {
                    RecordTelemetry(
                        ViewportPresentationTelemetryEventKind.CandidateWasted,
                        timestamp,
                        participant.Identity,
                        amount: checked((long)participant.Handle.CandidateRenderedFrames));
                }
            }

            runtime.Quarantine(new ViewportPresentationFailure(
                ViewportPresentationFailureCode.PublicationOutcomeAmbiguous,
                ViewportPresentationFailureDisposition.Ambiguous));
            var ambiguity = rollback.Failures.Count == 0
                ? failure
                : new AggregateException(failure, rollback.Failures).Message;
            return PublishedFailureExecution(Report(
                request,
                ViewportPresentationTransactionResult.Quarantined,
                preparedAt,
                publishedAt,
                renderedAt,
                timestamp,
                ambiguity));
        }

        RecordPrePublishFailure(proposed, prepared, outcome);
        runtime.Abort(new ViewportPresentationFailure(failureCode, failureDisposition));
        return FailureExecution(Report(
            request,
            result,
            preparedAt,
            publishedAt,
            renderedAt,
            Stopwatch.GetTimestamp(),
            failure));
    }

    private PrePublishRollbackResult RollbackBeforePublish(
        IReadOnlyList<PublishedParticipant> receipts,
        IReadOnlyList<PreparedParticipant> prepared,
        bool layoutApplied,
        Action rollbackLayout,
        ViewportPresentationTransactionGroupHookContext? groupHookContext)
    {
        var ambiguous = new HashSet<PublishedParticipant>();
        var failures = new List<Exception>();
        for (var index = receipts.Count - 1; index >= 0; index--)
        {
            var participant = receipts[index];
            if (participant.Receipt.IsQuarantined)
            {
                ambiguous.Add(participant);
                continue;
            }
            try
            {
                participant.Prepared.Participant.Endpoint.RollbackPreparedPresentation(
                    participant.Receipt);
            }
            catch (Exception exception)
            {
                ambiguous.Add(participant);
                failures.Add(exception);
                Trace.TraceError(
                    "Viewport transaction visual rollback failed for {0}: {1}",
                    participant.Prepared.Participant.ParticipantId,
                    exception);
            }
        }

        if (layoutApplied)
        {
            try
            {
                if (groupHookContext is not null)
                {
                    testHooks_?.AtRollbackStage(
                        ViewportPresentationRollbackHookPoint.BeforeLayoutRollback,
                        groupHookContext);
                }
                rollbackLayout();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
                foreach (var participant in receipts)
                {
                    ambiguous.Add(participant);
                }
                Trace.TraceError("Viewport transaction layout rollback failed: {0}", exception);
            }
        }

        if (ambiguous.Count != 0)
        {
            QuarantinePublishedParticipants(
                ambiguous.ToArray(),
                "Viewport publication rollback could not restore a deterministic visible front.");
        }

        var ambiguousHandles = ambiguous
            .Select(static participant => participant.Prepared.Handle)
            .ToHashSet();

        for (var index = prepared.Count - 1; index >= 0; index--)
        {
            var participant = prepared[index];
            if (ambiguousHandles.Contains(participant.Handle))
            {
                continue;
            }
            try
            {
                participant.Participant.Endpoint.CancelPreparedPresentation(participant.Handle);
            }
            catch (Exception exception)
            {
                Trace.TraceError(
                    "Viewport transaction cancellation failed for {0}: {1}",
                    participant.Participant.ParticipantId,
                    exception);
            }
        }
        return new PrePublishRollbackResult(ambiguous.ToArray(), failures);
    }

    private async Task<ViewportPresentationTransactionReport>
        CompletePublishedTransactionAsync(
            ViewportPresentationTransactionRequest request,
            ViewportPresentationTransactionState runtime,
            IReadOnlyList<PreparedParticipant> prepared,
            IReadOnlyList<PublishedParticipant> published,
            IReadOnlyList<Task> retirements,
            Task rendered,
            long preparedAt,
            long publishedAt,
            TaskCompletionSource<ViewportPresentationTransactionReport> retirementCompletion)
    {
        try
        {
            await rendered;
            var renderedAt = Stopwatch.GetTimestamp();
            foreach (var participant in published)
            {
                participant.Prepared.Participant.Endpoint.MarkPreparedPresentationRendered(
                    participant.Receipt,
                    renderedAt);
                RecordTelemetry(
                    ViewportPresentationTelemetryEventKind.Rendered,
                    renderedAt,
                    participant.Prepared.Identity);
            }

            foreach (var participant in prepared)
            {
                RequireApplied(runtime.MarkRendered(participant.StateId));
            }

            RequireApplied(runtime.BeginRetiring());
            var renderedReport = Report(
                request,
                ViewportPresentationTransactionResult.Committed,
                preparedAt,
                publishedAt,
                renderedAt,
                renderedAt,
                failure: null);
            _ = CompleteRetirementAsync(
                request,
                runtime,
                prepared,
                published,
                retirements,
                preparedAt,
                publishedAt,
                renderedAt,
                retirementCompletion);
            return renderedReport;
        }
        catch (Exception exception)
        {
            RecordOutcome(
                published.Select(static participant => participant.Prepared.Identity),
                ViewportPresentationTelemetryEventKind.Quarantined);
            runtime.Quarantine(new ViewportPresentationFailure(
                ViewportPresentationFailureCode.RenderOutcomeAmbiguous,
                ViewportPresentationFailureDisposition.Ambiguous));
            QuarantinePublishedParticipants(published, exception.Message);
            var quarantined = Report(
                request,
                ViewportPresentationTransactionResult.Quarantined,
                preparedAt,
                publishedAt,
                renderedAt: 0,
                completedAt: Stopwatch.GetTimestamp(),
                exception.Message);
            retirementCompletion.TrySetResult(quarantined);
            return quarantined;
        }
    }

    private static void QuarantinePublishedParticipants(
        IReadOnlyList<PublishedParticipant> published,
        string reason)
    {
        foreach (var participant in published)
        {
            try
            {
                participant.Prepared.Participant.Endpoint.QuarantinePublishedPresentation(
                    participant.Receipt,
                    reason);
            }
            catch (Exception exception)
            {
                Trace.TraceError(
                    "Viewport transaction quarantine failed for {0}: {1}",
                    participant.Prepared.Participant.ParticipantId,
                    exception);
            }
        }
    }

    private async Task CompleteRetirementAsync(
        ViewportPresentationTransactionRequest request,
        ViewportPresentationTransactionState runtime,
        IReadOnlyList<PreparedParticipant> prepared,
        IReadOnlyList<PublishedParticipant> published,
        IReadOnlyList<Task> retirements,
        long preparedAt,
        long publishedAt,
        long renderedAt,
        TaskCompletionSource<ViewportPresentationTransactionReport> completion)
    {
        try
        {
            await Task.WhenAll(retirements);
            foreach (var participant in prepared)
            {
                RequireApplied(runtime.MarkCompleted(participant.StateId));
            }
            completion.TrySetResult(Report(
                request,
                ViewportPresentationTransactionResult.Committed,
                preparedAt,
                publishedAt,
                renderedAt,
                Stopwatch.GetTimestamp(),
                failure: null));
        }
        catch (Exception exception)
        {
            RecordOutcome(
                published.Select(static participant => participant.Prepared.Identity),
                ViewportPresentationTelemetryEventKind.Quarantined);
            runtime.Quarantine(new ViewportPresentationFailure(
                ViewportPresentationFailureCode.RetirementOutcomeAmbiguous,
                ViewportPresentationFailureDisposition.Ambiguous));
            QuarantinePublishedParticipants(
                published,
                "A replaced viewport front did not retire deterministically.");
            completion.TrySetResult(Report(
                request,
                ViewportPresentationTransactionResult.Quarantined,
                preparedAt,
                publishedAt,
                renderedAt,
                Stopwatch.GetTimestamp(),
                exception.Message));
        }
    }

    private static ViewportPresentationTransactionExecution FailureExecution(
        ViewportPresentationTransactionReport report)
    {
        var completion = Task.FromResult(report);
        return new ViewportPresentationTransactionExecution(
            false,
            report,
            completion,
            completion);
    }

    private static ViewportPresentationTransactionExecution PublishedFailureExecution(
        ViewportPresentationTransactionReport finalReport)
    {
        var completion = Task.FromResult(finalReport);
        var publication = finalReport with
        {
            Result = ViewportPresentationTransactionResult.Published,
            RenderedAtTimestamp = 0,
            VisibleCommitAtTimestamp = finalReport.PublishedAtTimestamp,
            Failure = null,
        };
        return new ViewportPresentationTransactionExecution(
            true,
            publication,
            completion,
            completion);
    }

    private static ViewportPresentationTransactionReport Report(
        ViewportPresentationTransactionRequest request,
        ViewportPresentationTransactionResult result,
        long preparedAt,
        long publishedAt,
        long renderedAt,
        long completedAt,
        string? failure) =>
        new(
            request.TransactionId,
            result,
            request.Participants.Count,
            request.RequestedAtTimestamp,
            preparedAt,
            publishedAt,
            renderedAt,
            completedAt,
            failure);

    private sealed record PreparedParticipant(
        ViewportPresentationParticipant Participant,
        ViewportPresentationParticipantId StateId,
        ViewportPreparedPresentation Handle,
        ViewportPresentationTelemetryIdentity Identity,
        ViewportPresentationTransactionHookContext HookContext);

    private sealed record ProposedParticipant(
        ViewportPresentationParticipant Participant,
        ViewportPresentationParticipantId StateId,
        ViewportPresentationTelemetryIdentity Identity);

    private sealed record PublishedParticipant(
        PreparedParticipant Prepared,
        ViewportCompositionControl.PresentationPublishReceipt Receipt);

    private sealed record PrePublishRollbackResult(
        IReadOnlyList<PublishedParticipant> AmbiguousPublished,
        IReadOnlyList<Exception> Failures)
    {
        public bool IsAmbiguous =>
            AmbiguousPublished.Count != 0 || Failures.Count != 0;
    }

    private sealed class ViewportPresentationExtentMismatchException : Exception
    {
        public ViewportPresentationExtentMismatchException(string participantId)
            : base($"Viewport participant '{participantId}' did not reach its prepared exact extent.")
        {
        }
    }

    private void RecordPrePublishFailure(
        IReadOnlyList<ProposedParticipant> proposed,
        IReadOnlyList<PreparedParticipant> prepared,
        ViewportPresentationTelemetryEventKind outcome)
    {
        var timestamp = Stopwatch.GetTimestamp();
        RecordOutcome(proposed.Select(static participant => participant.Identity), outcome, timestamp);
        foreach (var participant in prepared)
        {
            RecordTelemetry(
                ViewportPresentationTelemetryEventKind.CandidateWasted,
                timestamp,
                participant.Identity,
                amount: checked((long)participant.Handle.CandidateRenderedFrames));
        }
    }

    private void RecordOutcome(
        IEnumerable<ViewportPresentationTelemetryIdentity> identities,
        ViewportPresentationTelemetryEventKind outcome,
        long? timestamp = null)
    {
        var recordedAt = timestamp ?? Stopwatch.GetTimestamp();
        foreach (var identity in identities)
        {
            RecordTelemetry(outcome, recordedAt, identity);
        }
    }

    private void RecordTelemetry(
        ViewportPresentationTelemetryEventKind kind,
        long timestamp,
        ViewportPresentationTelemetryIdentity identity,
        long amount = 0)
    {
        if (telemetry_ is null)
        {
            return;
        }

        var result = telemetry_.TryRecord(new ViewportPresentationTelemetryEvent(
            kind,
            timestamp,
            identity,
            amount));
        if (result != ViewportPresentationTelemetryRecordResult.Recorded)
        {
            Trace.TraceError(
                "Viewport presentation telemetry rejected {0} for transaction {1}.",
                kind,
                identity.TransactionId.Value);
        }
    }
}

internal sealed class ViewportPresentationRecoverableException : Exception
{
    public ViewportPresentationRecoverableException(string message)
        : base(message)
    {
    }
}

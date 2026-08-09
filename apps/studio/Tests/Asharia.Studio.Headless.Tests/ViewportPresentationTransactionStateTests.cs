using System.Linq;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationTransactionStateTests
{
    private static readonly ViewportPresentationParticipantId FirstParticipant = new(1);
    private static readonly ViewportPresentationParticipantId SecondParticipant = new(2);
    private static readonly ViewportPresentationAtomicScopeId AtomicScope = new(41);

    [Fact]
    public void Creation_rejects_participants_from_different_atomic_scopes()
    {
        var created = ViewportPresentationTransactionState.Create(
            new ViewportPresentationTransactionId(7),
            [
                new ViewportPresentationParticipantProposal(FirstParticipant, AtomicScope),
                new ViewportPresentationParticipantProposal(
                    SecondParticipant,
                    new ViewportPresentationAtomicScopeId(42)),
            ]);

        Assert.False(created.Succeeded);
        Assert.Null(created.State);
        Assert.True(created.Outcome.IsTerminal);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.TerminalFailure,
            created.Outcome.Kind);
        Assert.Equal(
            ViewportPresentationFailureCode.AtomicScopeMismatch,
            created.Outcome.Failure?.Code);
    }

    [Fact]
    public void Prepared_and_validated_barriers_gate_the_atomic_publish()
    {
        var state = CreateTransaction();

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.BeginPreparing(FirstParticipant).Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.MarkPrepared(FirstParticipant).Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Deferred,
            state.Validate(FirstParticipant).Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Preparing, state.Phase);

        state.BeginPreparing(SecondParticipant);
        state.MarkPrepared(SecondParticipant);
        Assert.Equal(ViewportPresentationTransactionPhase.Prepared, state.Phase);

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.Validate(FirstParticipant).Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Deferred,
            state.Publish().Disposition);
        Assert.Equal(0, state.PublishCount);
        Assert.Equal(ViewportPresentationTransactionPhase.Prepared, state.Phase);

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.Validate(SecondParticipant).Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Validated, state.Phase);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.Publish().Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Published, state.Phase);
        Assert.Equal(1, state.PublishCount);
        Assert.All(
            state.CaptureParticipants(),
            participant => Assert.Equal(
                ViewportPresentationTransactionPhase.Published,
                participant.Phase));
    }

    [Fact]
    public void Pre_publish_abort_aborts_every_participant_without_publishing()
    {
        var state = CreateTransaction();
        PrepareAll(state);
        state.Validate(FirstParticipant);
        var failure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.Backpressure,
            ViewportPresentationFailureDisposition.Recoverable,
            FirstParticipant);

        var aborted = state.Abort(failure);

        Assert.Equal(ViewportPresentationTransitionDisposition.Applied, aborted.Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Aborted, state.Phase);
        Assert.Equal(0, state.PublishCount);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.RecoverableFailure,
            state.Outcome.Kind);
        Assert.True(state.Outcome.IsTerminal);
        Assert.True(state.Outcome.CanRetry);
        Assert.Equal(failure, state.Outcome.Failure);
        Assert.All(
            state.CaptureParticipants(),
            participant => Assert.Equal(
                ViewportPresentationTransactionPhase.Aborted,
                participant.Phase));
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Rejected,
            state.Publish().Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.AlreadyApplied,
            state.Abort(failure).Disposition);
        Assert.Equal(0, state.PublishCount);
    }

    [Fact]
    public void Pre_publish_terminal_abort_has_a_typed_terminal_result()
    {
        var state = CreateTransaction();
        var failure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.PreparationRejected,
            ViewportPresentationFailureDisposition.Terminal,
            SecondParticipant);

        state.Abort(failure);

        Assert.Equal(ViewportPresentationTransactionPhase.Aborted, state.Phase);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.TerminalFailure,
            state.Outcome.Kind);
        Assert.False(state.Outcome.CanRetry);
        Assert.Equal(failure, state.Outcome.Failure);
    }

    [Fact]
    public void Pre_publish_ambiguous_failure_can_still_abort_without_publication()
    {
        var state = CreateTransaction();
        var failure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.PublicationOutcomeAmbiguous,
            ViewportPresentationFailureDisposition.Ambiguous,
            FirstParticipant);

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.Abort(failure).Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Aborted, state.Phase);
        Assert.Equal(0, state.PublishCount);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.TerminalFailure,
            state.Outcome.Kind);
    }

    [Fact]
    public void Post_publish_ambiguous_failure_can_only_quarantine_the_group()
    {
        var state = CreatePublishedTransaction();
        var terminalFailure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.ParticipantUnavailable,
            ViewportPresentationFailureDisposition.Terminal,
            FirstParticipant);
        var ambiguousFailure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.RenderOutcomeAmbiguous,
            ViewportPresentationFailureDisposition.Ambiguous,
            SecondParticipant);

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Rejected,
            state.Abort(terminalFailure).Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Published, state.Phase);
        Assert.Equal(1, state.PublishCount);

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.Quarantine(ambiguousFailure).Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Quarantined, state.Phase);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.TerminalFailure,
            state.Outcome.Kind);
        Assert.Equal(ambiguousFailure, state.Outcome.Failure);
        Assert.All(
            state.CaptureParticipants(),
            participant => Assert.Equal(
                ViewportPresentationTransactionPhase.Quarantined,
                participant.Phase));
        Assert.Equal(
            ViewportPresentationTransitionDisposition.AlreadyApplied,
            state.Quarantine(ambiguousFailure).Disposition);
        Assert.Equal(1, state.PublishCount);
    }

    [Fact]
    public void Render_and_retirement_barriers_complete_the_group()
    {
        var state = CreatePublishedTransaction();

        state.MarkRendered(FirstParticipant);
        Assert.Equal(ViewportPresentationTransactionPhase.Published, state.Phase);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Deferred,
            state.BeginRetiring().Disposition);

        state.MarkRendered(SecondParticipant);
        Assert.Equal(ViewportPresentationTransactionPhase.Rendered, state.Phase);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.BeginRetiring().Disposition);
        Assert.Equal(ViewportPresentationTransactionPhase.Retiring, state.Phase);

        state.MarkCompleted(FirstParticipant);
        Assert.Equal(ViewportPresentationTransactionPhase.Retiring, state.Phase);
        Assert.Equal(ViewportPresentationTransactionOutcomeKind.Pending, state.Outcome.Kind);
        state.MarkCompleted(SecondParticipant);

        Assert.Equal(ViewportPresentationTransactionPhase.Completed, state.Phase);
        Assert.Equal(
            ViewportPresentationTransactionOutcomeKind.Succeeded,
            state.Outcome.Kind);
        Assert.True(state.Outcome.IsTerminal);
        Assert.Null(state.Outcome.Failure);
        Assert.All(
            state.CaptureParticipants(),
            participant => Assert.Equal(
                ViewportPresentationTransactionPhase.Completed,
                participant.Phase));
    }

    [Fact]
    public async Task Publish_transition_is_exact_once_under_concurrent_calls()
    {
        var state = CreateTransaction();
        PrepareAll(state);
        state.Validate(FirstParticipant);
        state.Validate(SecondParticipant);

        var results = await Task.WhenAll(
            Enumerable.Range(0, 16)
                .Select(_ => Task.Run(state.Publish)));

        Assert.Single(
            results,
            result => result.Disposition == ViewportPresentationTransitionDisposition.Applied);
        Assert.Equal(
            15,
            results.Count(
                result => result.Disposition ==
                    ViewportPresentationTransitionDisposition.AlreadyApplied));
        Assert.Equal(1, state.PublishCount);
        Assert.Equal(ViewportPresentationTransactionPhase.Published, state.Phase);
    }

    [Fact]
    public void Participant_transitions_are_idempotent_and_unknown_identity_is_rejected()
    {
        var state = CreateTransaction();

        Assert.Equal(
            ViewportPresentationTransitionDisposition.Applied,
            state.BeginPreparing(FirstParticipant).Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.AlreadyApplied,
            state.BeginPreparing(FirstParticipant).Disposition);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Rejected,
            state.BeginPreparing(new ViewportPresentationParticipantId(99)).Disposition);
        Assert.True(state.TryGetParticipantPhase(FirstParticipant, out var phase));
        Assert.Equal(ViewportPresentationTransactionPhase.Preparing, phase);
        Assert.False(
            state.TryGetParticipantPhase(
                new ViewportPresentationParticipantId(99),
                out _));
    }

    private static ViewportPresentationTransactionState CreateTransaction()
    {
        var created = ViewportPresentationTransactionState.Create(
            new ViewportPresentationTransactionId(7),
            [
                new ViewportPresentationParticipantProposal(FirstParticipant, AtomicScope),
                new ViewportPresentationParticipantProposal(SecondParticipant, AtomicScope),
            ]);
        Assert.True(created.Succeeded);
        Assert.Equal(ViewportPresentationTransactionOutcomeKind.Pending, created.Outcome.Kind);
        return Assert.IsType<ViewportPresentationTransactionState>(created.State);
    }

    private static void PrepareAll(ViewportPresentationTransactionState state)
    {
        state.BeginPreparing(FirstParticipant);
        state.BeginPreparing(SecondParticipant);
        state.MarkPrepared(FirstParticipant);
        state.MarkPrepared(SecondParticipant);
    }

    private static ViewportPresentationTransactionState CreatePublishedTransaction()
    {
        var state = CreateTransaction();
        PrepareAll(state);
        state.Validate(FirstParticipant);
        state.Validate(SecondParticipant);
        state.Publish();
        return state;
    }
}

using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal readonly record struct ViewportPresentationTransactionId(ulong Value)
{
    public bool IsValid => Value != 0;
}

internal readonly record struct ViewportPresentationParticipantId(ulong Value)
{
    public bool IsValid => Value != 0;
}

internal readonly record struct ViewportPresentationAtomicScopeId(ulong Value)
{
    public bool IsValid => Value != 0;
}

internal readonly record struct ViewportPresentationParticipantProposal(
    ViewportPresentationParticipantId ParticipantId,
    ViewportPresentationAtomicScopeId AtomicScopeId);

internal enum ViewportPresentationTransactionPhase
{
    Proposal,
    Preparing,
    Prepared,
    Validated,
    Published,
    Rendered,
    Retiring,
    Completed,
    Aborted,
    Quarantined,
}

internal enum ViewportPresentationFailureDisposition
{
    Recoverable,
    Terminal,
    Ambiguous,
}

internal enum ViewportPresentationFailureCode
{
    InvalidTransactionIdentity,
    NoParticipants,
    InvalidParticipantIdentity,
    DuplicateParticipantIdentity,
    InvalidAtomicScopeIdentity,
    AtomicScopeMismatch,
    Cancelled,
    Backpressure,
    PreparationRejected,
    ValidationRejected,
    ParticipantUnavailable,
    PublicationOutcomeAmbiguous,
    RenderOutcomeAmbiguous,
    RetirementOutcomeAmbiguous,
}

internal readonly record struct ViewportPresentationFailure(
    ViewportPresentationFailureCode Code,
    ViewportPresentationFailureDisposition Disposition,
    ViewportPresentationParticipantId? ParticipantId = null);

internal enum ViewportPresentationTransactionOutcomeKind
{
    Pending,
    Succeeded,
    RecoverableFailure,
    TerminalFailure,
}

internal readonly record struct ViewportPresentationTransactionOutcome(
    ViewportPresentationTransactionOutcomeKind Kind,
    ViewportPresentationTransactionPhase Phase,
    ViewportPresentationFailure? Failure)
{
    public bool IsTerminal => Kind != ViewportPresentationTransactionOutcomeKind.Pending;

    public bool CanRetry => Kind == ViewportPresentationTransactionOutcomeKind.RecoverableFailure;
}

internal enum ViewportPresentationTransitionDisposition
{
    Applied,
    AlreadyApplied,
    Deferred,
    Rejected,
}

internal readonly record struct ViewportPresentationTransitionResult(
    ViewportPresentationTransitionDisposition Disposition,
    ViewportPresentationTransactionPhase Phase,
    ViewportPresentationTransactionOutcome TransactionOutcome)
{
    public bool AppliedExactlyOnce => Disposition == ViewportPresentationTransitionDisposition.Applied;
}

internal readonly record struct ViewportPresentationParticipantSnapshot(
    ViewportPresentationParticipantId ParticipantId,
    ViewportPresentationAtomicScopeId AtomicScopeId,
    ViewportPresentationTransactionPhase Phase);

internal readonly record struct ViewportPresentationTransactionCreationResult(
    ViewportPresentationTransactionState? State,
    ViewportPresentationTransactionOutcome Outcome)
{
    public bool Succeeded => State is not null;
}

internal sealed class ViewportPresentationTransactionState
{
    private readonly object gate_ = new();
    private readonly Dictionary<ViewportPresentationParticipantId, ParticipantState> participants_;
    private readonly ViewportPresentationParticipantId[] participantOrder_;
    private ViewportPresentationTransactionPhase phase_;
    private ViewportPresentationTransactionOutcome outcome_;
    private int publishCount_;

    private ViewportPresentationTransactionState(
        ViewportPresentationTransactionId transactionId,
        ViewportPresentationAtomicScopeId atomicScopeId,
        IReadOnlyList<ViewportPresentationParticipantProposal> proposals)
    {
        TransactionId = transactionId;
        AtomicScopeId = atomicScopeId;
        participants_ = new Dictionary<ViewportPresentationParticipantId, ParticipantState>(
            proposals.Count);
        participantOrder_ = new ViewportPresentationParticipantId[proposals.Count];
        for (var index = 0; index < proposals.Count; index++)
        {
            var proposal = proposals[index];
            participantOrder_[index] = proposal.ParticipantId;
            participants_.Add(
                proposal.ParticipantId,
                new ParticipantState(proposal.AtomicScopeId));
        }

        phase_ = ViewportPresentationTransactionPhase.Proposal;
        outcome_ = PendingOutcome(phase_);
    }

    public ViewportPresentationTransactionId TransactionId { get; }

    public ViewportPresentationAtomicScopeId AtomicScopeId { get; }

    public ViewportPresentationTransactionPhase Phase
    {
        get
        {
            lock (gate_)
            {
                return phase_;
            }
        }
    }

    public ViewportPresentationTransactionOutcome Outcome
    {
        get
        {
            lock (gate_)
            {
                return outcome_;
            }
        }
    }

    public int ParticipantCount => participantOrder_.Length;

    public int PublishCount
    {
        get
        {
            lock (gate_)
            {
                return publishCount_;
            }
        }
    }

    public static ViewportPresentationTransactionCreationResult Create(
        ViewportPresentationTransactionId transactionId,
        IReadOnlyList<ViewportPresentationParticipantProposal>? proposals)
    {
        if (!transactionId.IsValid)
        {
            return CreationFailure(ViewportPresentationFailureCode.InvalidTransactionIdentity);
        }

        if (proposals is null || proposals.Count == 0)
        {
            return CreationFailure(ViewportPresentationFailureCode.NoParticipants);
        }

        var participantIds = new HashSet<ViewportPresentationParticipantId>();
        ViewportPresentationAtomicScopeId? atomicScopeId = null;
        foreach (var proposal in proposals)
        {
            if (!proposal.ParticipantId.IsValid)
            {
                return CreationFailure(
                    ViewportPresentationFailureCode.InvalidParticipantIdentity,
                    proposal.ParticipantId);
            }
            if (!participantIds.Add(proposal.ParticipantId))
            {
                return CreationFailure(
                    ViewportPresentationFailureCode.DuplicateParticipantIdentity,
                    proposal.ParticipantId);
            }
            if (!proposal.AtomicScopeId.IsValid)
            {
                return CreationFailure(
                    ViewportPresentationFailureCode.InvalidAtomicScopeIdentity,
                    proposal.ParticipantId);
            }
            if (atomicScopeId is { } expectedScope && expectedScope != proposal.AtomicScopeId)
            {
                return CreationFailure(
                    ViewportPresentationFailureCode.AtomicScopeMismatch,
                    proposal.ParticipantId);
            }

            atomicScopeId = proposal.AtomicScopeId;
        }

        var state = new ViewportPresentationTransactionState(
            transactionId,
            atomicScopeId!.Value,
            proposals);
        return new ViewportPresentationTransactionCreationResult(state, state.Outcome);
    }

    public IReadOnlyList<ViewportPresentationParticipantSnapshot> CaptureParticipants()
    {
        lock (gate_)
        {
            var snapshots = new ViewportPresentationParticipantSnapshot[participantOrder_.Length];
            for (var index = 0; index < participantOrder_.Length; index++)
            {
                var participantId = participantOrder_[index];
                var participant = participants_[participantId];
                snapshots[index] = new ViewportPresentationParticipantSnapshot(
                    participantId,
                    participant.AtomicScopeId,
                    participant.Phase);
            }

            return snapshots;
        }
    }

    public bool TryGetParticipantPhase(
        ViewportPresentationParticipantId participantId,
        out ViewportPresentationTransactionPhase phase)
    {
        lock (gate_)
        {
            if (!participants_.TryGetValue(participantId, out var participant))
            {
                phase = default;
                return false;
            }

            phase = participant.Phase;
            return true;
        }
    }

    public ViewportPresentationTransitionResult BeginPreparing(
        ViewportPresentationParticipantId participantId)
    {
        lock (gate_)
        {
            if (!TryGetActiveParticipant(participantId, out var participant, out var rejected))
            {
                return rejected;
            }
            if (HasReachedPreparing(participant.Phase))
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (participant.Phase != ViewportPresentationTransactionPhase.Proposal)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            participant.Phase = ViewportPresentationTransactionPhase.Preparing;
            if (phase_ == ViewportPresentationTransactionPhase.Proposal)
            {
                SetPendingPhase(ViewportPresentationTransactionPhase.Preparing);
            }
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult MarkPrepared(
        ViewportPresentationParticipantId participantId)
    {
        lock (gate_)
        {
            if (!TryGetActiveParticipant(participantId, out var participant, out var rejected))
            {
                return rejected;
            }
            if (HasReachedPrepared(participant.Phase))
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (participant.Phase != ViewportPresentationTransactionPhase.Preparing)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            participant.Phase = ViewportPresentationTransactionPhase.Prepared;
            if (AllParticipantsAre(ViewportPresentationTransactionPhase.Prepared))
            {
                SetPendingPhase(ViewportPresentationTransactionPhase.Prepared);
            }
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult Validate(
        ViewportPresentationParticipantId participantId)
    {
        lock (gate_)
        {
            if (!TryGetActiveParticipant(participantId, out var participant, out var rejected))
            {
                return rejected;
            }
            if (HasReachedValidated(participant.Phase))
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (phase_ != ViewportPresentationTransactionPhase.Prepared)
            {
                return Transition(ViewportPresentationTransitionDisposition.Deferred);
            }
            if (participant.Phase != ViewportPresentationTransactionPhase.Prepared)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            participant.Phase = ViewportPresentationTransactionPhase.Validated;
            if (AllParticipantsAre(ViewportPresentationTransactionPhase.Validated))
            {
                SetPendingPhase(ViewportPresentationTransactionPhase.Validated);
            }
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult Publish()
    {
        lock (gate_)
        {
            if (IsTerminal(phase_))
            {
                return phase_ == ViewportPresentationTransactionPhase.Completed
                    ? Transition(ViewportPresentationTransitionDisposition.AlreadyApplied)
                    : Transition(ViewportPresentationTransitionDisposition.Rejected);
            }
            if (HasGroupPublished(phase_))
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (phase_ != ViewportPresentationTransactionPhase.Validated)
            {
                return Transition(ViewportPresentationTransitionDisposition.Deferred);
            }

            foreach (var participant in participants_.Values)
            {
                participant.Phase = ViewportPresentationTransactionPhase.Published;
            }
            publishCount_++;
            SetPendingPhase(ViewportPresentationTransactionPhase.Published);
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult MarkRendered(
        ViewportPresentationParticipantId participantId)
    {
        lock (gate_)
        {
            if (!TryGetActiveParticipant(participantId, out var participant, out var rejected))
            {
                return rejected;
            }
            if (HasReachedRendered(participant.Phase))
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (phase_ != ViewportPresentationTransactionPhase.Published
                || participant.Phase != ViewportPresentationTransactionPhase.Published)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            participant.Phase = ViewportPresentationTransactionPhase.Rendered;
            if (AllParticipantsAre(ViewportPresentationTransactionPhase.Rendered))
            {
                SetPendingPhase(ViewportPresentationTransactionPhase.Rendered);
            }
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult BeginRetiring()
    {
        lock (gate_)
        {
            if (IsTerminal(phase_))
            {
                return phase_ == ViewportPresentationTransactionPhase.Completed
                    ? Transition(ViewportPresentationTransitionDisposition.AlreadyApplied)
                    : Transition(ViewportPresentationTransitionDisposition.Rejected);
            }
            if (phase_ == ViewportPresentationTransactionPhase.Retiring)
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (phase_ == ViewportPresentationTransactionPhase.Published)
            {
                return Transition(ViewportPresentationTransitionDisposition.Deferred);
            }
            if (phase_ != ViewportPresentationTransactionPhase.Rendered)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            foreach (var participant in participants_.Values)
            {
                participant.Phase = ViewportPresentationTransactionPhase.Retiring;
            }
            SetPendingPhase(ViewportPresentationTransactionPhase.Retiring);
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult MarkCompleted(
        ViewportPresentationParticipantId participantId)
    {
        lock (gate_)
        {
            if (!participants_.TryGetValue(participantId, out var participant))
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }
            if (participant.Phase == ViewportPresentationTransactionPhase.Completed)
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (IsTerminal(phase_)
                || phase_ != ViewportPresentationTransactionPhase.Retiring
                || participant.Phase != ViewportPresentationTransactionPhase.Retiring)
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            participant.Phase = ViewportPresentationTransactionPhase.Completed;
            if (AllParticipantsAre(ViewportPresentationTransactionPhase.Completed))
            {
                phase_ = ViewportPresentationTransactionPhase.Completed;
                outcome_ = new ViewportPresentationTransactionOutcome(
                    ViewportPresentationTransactionOutcomeKind.Succeeded,
                    phase_,
                    Failure: null);
            }
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult Abort(ViewportPresentationFailure failure)
    {
        lock (gate_)
        {
            if (phase_ == ViewportPresentationTransactionPhase.Aborted)
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (!FailureParticipantIsKnown(failure)
                || HasGroupPublished(phase_)
                || IsTerminal(phase_))
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            foreach (var participant in participants_.Values)
            {
                participant.Phase = ViewportPresentationTransactionPhase.Aborted;
            }
            phase_ = ViewportPresentationTransactionPhase.Aborted;
            outcome_ = new ViewportPresentationTransactionOutcome(
                failure.Disposition == ViewportPresentationFailureDisposition.Recoverable
                    ? ViewportPresentationTransactionOutcomeKind.RecoverableFailure
                    : ViewportPresentationTransactionOutcomeKind.TerminalFailure,
                phase_,
                failure);
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    public ViewportPresentationTransitionResult Quarantine(ViewportPresentationFailure failure)
    {
        lock (gate_)
        {
            if (phase_ == ViewportPresentationTransactionPhase.Quarantined)
            {
                return Transition(ViewportPresentationTransitionDisposition.AlreadyApplied);
            }
            if (failure.Disposition != ViewportPresentationFailureDisposition.Ambiguous
                || !FailureParticipantIsKnown(failure)
                || !HasGroupPublished(phase_)
                || IsTerminal(phase_))
            {
                return Transition(ViewportPresentationTransitionDisposition.Rejected);
            }

            foreach (var participant in participants_.Values)
            {
                if (participant.Phase != ViewportPresentationTransactionPhase.Completed)
                {
                    participant.Phase = ViewportPresentationTransactionPhase.Quarantined;
                }
            }
            phase_ = ViewportPresentationTransactionPhase.Quarantined;
            outcome_ = new ViewportPresentationTransactionOutcome(
                ViewportPresentationTransactionOutcomeKind.TerminalFailure,
                phase_,
                failure);
            return Transition(ViewportPresentationTransitionDisposition.Applied);
        }
    }

    private bool TryGetActiveParticipant(
        ViewportPresentationParticipantId participantId,
        out ParticipantState participant,
        out ViewportPresentationTransitionResult rejected)
    {
        if (!participants_.TryGetValue(participantId, out participant!))
        {
            rejected = Transition(ViewportPresentationTransitionDisposition.Rejected);
            return false;
        }

        rejected = default;
        return true;
    }

    private bool AllParticipantsAre(ViewportPresentationTransactionPhase phase)
    {
        return participants_.Values.All(participant => participant.Phase == phase);
    }

    private bool FailureParticipantIsKnown(ViewportPresentationFailure failure)
    {
        return failure.ParticipantId is not { } participantId
            || participants_.ContainsKey(participantId);
    }

    private void SetPendingPhase(ViewportPresentationTransactionPhase phase)
    {
        phase_ = phase;
        outcome_ = PendingOutcome(phase);
    }

    private ViewportPresentationTransitionResult Transition(
        ViewportPresentationTransitionDisposition disposition)
    {
        return new ViewportPresentationTransitionResult(disposition, phase_, outcome_);
    }

    private static bool HasReachedPreparing(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Preparing
            or ViewportPresentationTransactionPhase.Prepared
            or ViewportPresentationTransactionPhase.Validated
            or ViewportPresentationTransactionPhase.Published
            or ViewportPresentationTransactionPhase.Rendered
            or ViewportPresentationTransactionPhase.Retiring
            or ViewportPresentationTransactionPhase.Completed;
    }

    private static bool HasReachedPrepared(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Prepared
            or ViewportPresentationTransactionPhase.Validated
            or ViewportPresentationTransactionPhase.Published
            or ViewportPresentationTransactionPhase.Rendered
            or ViewportPresentationTransactionPhase.Retiring
            or ViewportPresentationTransactionPhase.Completed;
    }

    private static bool HasReachedValidated(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Validated
            or ViewportPresentationTransactionPhase.Published
            or ViewportPresentationTransactionPhase.Rendered
            or ViewportPresentationTransactionPhase.Retiring
            or ViewportPresentationTransactionPhase.Completed;
    }

    private static bool HasReachedRendered(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Rendered
            or ViewportPresentationTransactionPhase.Retiring
            or ViewportPresentationTransactionPhase.Completed;
    }

    private static bool HasGroupPublished(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Published
            or ViewportPresentationTransactionPhase.Rendered
            or ViewportPresentationTransactionPhase.Retiring
            or ViewportPresentationTransactionPhase.Completed
            or ViewportPresentationTransactionPhase.Quarantined;
    }

    private static bool IsTerminal(ViewportPresentationTransactionPhase phase)
    {
        return phase is ViewportPresentationTransactionPhase.Completed
            or ViewportPresentationTransactionPhase.Aborted
            or ViewportPresentationTransactionPhase.Quarantined;
    }

    private static ViewportPresentationTransactionOutcome PendingOutcome(
        ViewportPresentationTransactionPhase phase)
    {
        return new ViewportPresentationTransactionOutcome(
            ViewportPresentationTransactionOutcomeKind.Pending,
            phase,
            Failure: null);
    }

    private static ViewportPresentationTransactionCreationResult CreationFailure(
        ViewportPresentationFailureCode code,
        ViewportPresentationParticipantId? participantId = null)
    {
        var failure = new ViewportPresentationFailure(
            code,
            ViewportPresentationFailureDisposition.Terminal,
            participantId);
        return new ViewportPresentationTransactionCreationResult(
            State: null,
            new ViewportPresentationTransactionOutcome(
                ViewportPresentationTransactionOutcomeKind.TerminalFailure,
                ViewportPresentationTransactionPhase.Aborted,
                failure));
    }

    private sealed class ParticipantState
    {
        public ParticipantState(ViewportPresentationAtomicScopeId atomicScopeId)
        {
            AtomicScopeId = atomicScopeId;
        }

        public ViewportPresentationAtomicScopeId AtomicScopeId { get; }

        public ViewportPresentationTransactionPhase Phase { get; set; } =
            ViewportPresentationTransactionPhase.Proposal;
    }
}

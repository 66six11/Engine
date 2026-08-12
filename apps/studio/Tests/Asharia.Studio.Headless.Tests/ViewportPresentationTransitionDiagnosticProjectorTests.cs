using System;
using System.Linq;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationTransitionDiagnosticProjectorTests
{
    private static readonly ViewportPresentationParticipantId ParticipantId = new(1);
    private static readonly ViewportPresentationAtomicScopeId AtomicScopeId = new(41);

    [Theory]
    [InlineData(0, true)]
    [InlineData(1, true)]
    [InlineData(2, true)]
    [InlineData(3, false)]
    [InlineData(4, true)]
    [InlineData(5, false)]
    [InlineData(6, true)]
    public void Every_required_coordinator_edge_publishes_exact_context_before_throwing(
        int edgeValue,
        bool participantScoped)
    {
        var edge = (ViewportPresentationTransitionEdge)edgeValue;
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var session = new ViewportPresentationTransitionDiagnosticSession(
            hub,
            new ViewportPresentationTransactionId(7));
        var state = CreateTransaction();
        state.Abort(new ViewportPresentationFailure(
            ViewportPresentationFailureCode.ValidationRejected,
            ViewportPresentationFailureDisposition.Terminal,
            ParticipantId));
        var transition = state.Publish();
        var identity = Identity();

        Assert.Throws<InvalidOperationException>(() => session.RequireApplied(
            edge,
            transition,
            identity,
            participantScoped ? ParticipantId : null));

        var record = Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
        Assert.Equal(edge.ToString(), Attribute(record, "edge"));
        Assert.Equal(identity.EndpointId.Value, Attribute(record, "endpointId"));
        Assert.Equal(identity.TransactionId.Value.ToString(), Attribute(record, "transactionId"));
        Assert.Equal(identity.Generation.ToString(), Attribute(record, "presentationGeneration"));
        var participantAttribute = record.Attributes
            .SingleOrDefault(attribute => attribute.Name == "participantId");
        Assert.Equal(
            participantScoped ? ParticipantId.Value.ToString() : null,
            participantAttribute.Value);
    }

    [Fact]
    public void Rejected_required_edge_preserves_typed_failure_and_correlation_context()
    {
        var state = CreateTransaction();
        var failure = new ViewportPresentationFailure(
            ViewportPresentationFailureCode.ValidationRejected,
            ViewportPresentationFailureDisposition.Terminal,
            ParticipantId);
        state.Abort(failure);
        var transition = state.Publish();
        var context = Context(ViewportPresentationTransitionEdge.Publish);

        var diagnostic = ViewportPresentationTransitionDiagnosticProjector
            .ProjectRequiredEdgeFailure(context, transition);

        Assert.NotNull(diagnostic);
        Assert.Equal(
            "studio.viewport.presentation.transition.rejected",
            diagnostic.Code);
        Assert.Equal(StudioDiagnosticSeverity.Error, diagnostic.Severity);
        Assert.Equal(StudioDiagnosticChannel.Problem, diagnostic.Channel);
        Assert.Equal(context.SessionId.Value.ToString("D"), diagnostic.Context.Scope.Identity);
        Assert.Equal((long)context.Epoch, diagnostic.Context.Scope.Generation);
        Assert.Equal(context.OperationId, diagnostic.Context.OperationId);
        Assert.Equal(context.CorrelationId, diagnostic.Context.CorrelationId);
        Assert.Equal(context.ParentCorrelationId, diagnostic.Context.ParentCorrelationId);
        Assert.Equal(
            ViewportPresentationTransitionEdge.Publish.ToString(),
            Attribute(diagnostic, "edge"));
        Assert.Equal(
            ViewportPresentationFailureCode.ValidationRejected.ToString(),
            Attribute(diagnostic, "failureCode"));
        Assert.Equal(
            ViewportPresentationFailureDisposition.Terminal.ToString(),
            Attribute(diagnostic, "failureDisposition"));
        Assert.Equal(
            ParticipantId.Value.ToString(System.Globalization.CultureInfo.InvariantCulture),
            Attribute(diagnostic, "failureParticipantId"));
    }

    [Fact]
    public void Deferred_required_edge_is_a_distinct_problem()
    {
        var state = CreateTransaction(includeSecondParticipant: true);
        state.BeginPreparing(ParticipantId);
        state.MarkPrepared(ParticipantId);
        var transition = state.Validate(ParticipantId);

        var diagnostic = ViewportPresentationTransitionDiagnosticProjector
            .ProjectRequiredEdgeFailure(
                Context(ViewportPresentationTransitionEdge.Validate),
                transition);

        Assert.NotNull(diagnostic);
        Assert.Equal(
            "studio.viewport.presentation.transition.deferred",
            diagnostic.Code);
        Assert.Equal(
            ViewportPresentationTransitionDisposition.Deferred.ToString(),
            Attribute(diagnostic, "transitionDisposition"));
        Assert.Equal(
            ViewportPresentationTransactionPhase.Preparing.ToString(),
            Attribute(diagnostic, "phase"));
    }

    [Fact]
    public void Applied_and_idempotent_edges_do_not_create_diagnostics()
    {
        var state = CreateTransaction();
        var applied = state.BeginPreparing(ParticipantId);
        var alreadyApplied = state.BeginPreparing(ParticipantId);
        var context = Context(ViewportPresentationTransitionEdge.BeginPreparing);

        Assert.Null(
            ViewportPresentationTransitionDiagnosticProjector
                .ProjectRequiredEdgeFailure(context, applied));
        Assert.Null(
            ViewportPresentationTransitionDiagnosticProjector
                .ProjectRequiredEdgeFailure(context, alreadyApplied));
    }

    [Fact]
    public void Required_edge_session_publishes_once_and_throws_for_the_same_edge()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var session = new ViewportPresentationTransitionDiagnosticSession(
            hub,
            new ViewportPresentationTransactionId(7));
        var state = CreateTransaction();
        state.Abort(new ViewportPresentationFailure(
            ViewportPresentationFailureCode.ValidationRejected,
            ViewportPresentationFailureDisposition.Terminal,
            ParticipantId));
        var transition = state.Publish();
        var identity = Identity();

        Assert.Throws<InvalidOperationException>(() => session.RequireApplied(
            ViewportPresentationTransitionEdge.Publish,
            transition,
            identity));
        Assert.Throws<InvalidOperationException>(() => session.RequireApplied(
            ViewportPresentationTransitionEdge.Publish,
            transition,
            identity));

        var records = hub.ReadDiagnostics(maxCount: 4).Items;
        var record = Assert.Single(records);
        Assert.Equal("studio.viewport.presentation.transition.rejected", record.Code);
        Assert.Equal(Guid.Parse(record.Context.Scope.Identity), identity.SessionId.Value);
        Assert.Equal((long)identity.Epoch, record.Context.Scope.Generation);
        Assert.NotNull(record.Context.OperationId);
        Assert.NotNull(record.Context.CorrelationId);
    }

    [Fact]
    public void Applied_edge_session_does_not_publish_or_throw()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var session = new ViewportPresentationTransitionDiagnosticSession(
            hub,
            new ViewportPresentationTransactionId(7));
        var state = CreateTransaction();

        session.RequireApplied(
            ViewportPresentationTransitionEdge.BeginPreparing,
            state.BeginPreparing(ParticipantId),
            Identity(),
            ParticipantId);

        Assert.Empty(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Fact]
    public void Required_edges_in_one_transaction_share_operation_and_correlation_context()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var session = new ViewportPresentationTransitionDiagnosticSession(
            hub,
            new ViewportPresentationTransactionId(7));
        var state = CreateTransaction();
        state.Abort(new ViewportPresentationFailure(
            ViewportPresentationFailureCode.ValidationRejected,
            ViewportPresentationFailureDisposition.Terminal,
            ParticipantId));
        var transition = state.Publish();
        var identity = Identity();

        Assert.Throws<InvalidOperationException>(() => session.RequireApplied(
            ViewportPresentationTransitionEdge.Publish,
            transition,
            identity));
        Assert.Throws<InvalidOperationException>(() => session.RequireApplied(
            ViewportPresentationTransitionEdge.BeginRetiring,
            transition,
            identity));

        var records = hub.ReadDiagnostics(maxCount: 4).Items;
        Assert.Equal(2, records.Length);
        Assert.Equal(records[0].Context.OperationId, records[1].Context.OperationId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
    }

    [Fact]
    public void Diagnostic_context_rejects_epoch_that_cannot_be_a_scope_generation()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new ViewportPresentationTransitionDiagnosticContext(
                ViewportPresentationTransitionEdge.Publish,
                new ViewportPresentationEndpointId("scene-view"),
                ViewportSessionId.Create(),
                checked((ulong)long.MaxValue + 1),
                new ViewportPresentationTransactionId(7),
                generation: 11,
                Guid.NewGuid(),
                Guid.NewGuid()));
    }

    private static ViewportPresentationTransitionDiagnosticContext Context(
        ViewportPresentationTransitionEdge edge) =>
        new(
            edge,
            new ViewportPresentationEndpointId("scene-view"),
            ViewportSessionId.Create(),
            epoch: 3,
            new ViewportPresentationTransactionId(7),
            generation: 11,
            Guid.NewGuid(),
            Guid.NewGuid(),
            Guid.NewGuid(),
            ParticipantId);

    private static ViewportPresentationTelemetryIdentity Identity() =>
        new(
            new ViewportPresentationEndpointId("scene-view"),
            ViewportSessionId.Create(),
            Epoch: 3,
            new ViewportPresentationTransactionId(7),
            Generation: 11,
            new ViewportExtent(640, 480));

    private static ViewportPresentationTransactionState CreateTransaction(
        bool includeSecondParticipant = false)
    {
        var proposals = includeSecondParticipant
            ? new[]
            {
                new ViewportPresentationParticipantProposal(ParticipantId, AtomicScopeId),
                new ViewportPresentationParticipantProposal(
                    new ViewportPresentationParticipantId(2),
                    AtomicScopeId),
            }
            :
            [
                new ViewportPresentationParticipantProposal(ParticipantId, AtomicScopeId),
            ];
        var created = ViewportPresentationTransactionState.Create(
            new ViewportPresentationTransactionId(7),
            proposals);
        return Assert.IsType<ViewportPresentationTransactionState>(created.State);
    }

    private static string Attribute(
        StudioDiagnosticWrite diagnostic,
        string name) =>
        diagnostic.Attributes.Single(attribute => attribute.Name == name).Value;

    private static string Attribute(
        StudioDiagnosticRecord diagnostic,
        string name) =>
        diagnostic.Attributes.Single(attribute => attribute.Name == name).Value;
}

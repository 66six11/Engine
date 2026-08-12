using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal enum ViewportPresentationTransitionEdge
{
    BeginPreparing,
    MarkPrepared,
    Validate,
    Publish,
    MarkRendered,
    BeginRetiring,
    MarkCompleted,
}

internal sealed record ViewportPresentationTransitionDiagnosticContext
{
    public ViewportPresentationTransitionDiagnosticContext(
        ViewportPresentationTransitionEdge edge,
        ViewportPresentationEndpointId endpointId,
        ViewportSessionId sessionId,
        ulong epoch,
        ViewportPresentationTransactionId transactionId,
        ulong generation,
        Guid operationId,
        Guid correlationId,
        Guid? parentCorrelationId = null,
        ViewportPresentationParticipantId? participantId = null)
    {
        if (!Enum.IsDefined(edge))
        {
            throw new ArgumentOutOfRangeException(nameof(edge));
        }
        if (!endpointId.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation endpoint id must be valid.",
                nameof(endpointId));
        }
        if (!sessionId.IsValid)
        {
            throw new ArgumentException(
                "Viewport session id must be valid.",
                nameof(sessionId));
        }
        if (epoch > long.MaxValue)
        {
            throw new ArgumentOutOfRangeException(
                nameof(epoch),
                "Viewport epoch must fit the diagnostic scope generation.");
        }
        if (!transactionId.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation transaction id must be valid.",
                nameof(transactionId));
        }
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Operation id must not be empty.",
                nameof(operationId));
        }
        if (correlationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Correlation id must not be empty.",
                nameof(correlationId));
        }
        if (parentCorrelationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Parent correlation id must not be empty.",
                nameof(parentCorrelationId));
        }
        if (participantId is ViewportPresentationParticipantId participant &&
            !participant.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation participant id must be valid.",
                nameof(participantId));
        }

        Edge = edge;
        EndpointId = endpointId;
        SessionId = sessionId;
        Epoch = epoch;
        TransactionId = transactionId;
        Generation = generation;
        OperationId = operationId;
        CorrelationId = correlationId;
        ParentCorrelationId = parentCorrelationId;
        ParticipantId = participantId;
    }

    public ViewportPresentationTransitionEdge Edge { get; }

    public ViewportPresentationEndpointId EndpointId { get; }

    public ViewportSessionId SessionId { get; }

    public ulong Epoch { get; }

    public ViewportPresentationTransactionId TransactionId { get; }

    public ulong Generation { get; }

    public Guid OperationId { get; }

    public Guid CorrelationId { get; }

    public Guid? ParentCorrelationId { get; }

    public ViewportPresentationParticipantId? ParticipantId { get; }
}

internal static class ViewportPresentationTransitionDiagnosticProjector
{
    public static StudioDiagnosticWrite? ProjectRequiredEdgeFailure(
        ViewportPresentationTransitionDiagnosticContext context,
        ViewportPresentationTransitionResult transition)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (transition.Disposition is
            ViewportPresentationTransitionDisposition.Applied or
            ViewportPresentationTransitionDisposition.AlreadyApplied)
        {
            return null;
        }

        var code = transition.Disposition switch
        {
            ViewportPresentationTransitionDisposition.Deferred =>
                "studio.viewport.presentation.transition.deferred",
            ViewportPresentationTransitionDisposition.Rejected =>
                "studio.viewport.presentation.transition.rejected",
            _ => throw new ArgumentOutOfRangeException(
                nameof(transition),
                transition.Disposition,
                "Unknown viewport presentation transition disposition."),
        };
        var attributes = CreateAttributes(context, transition);
        return new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            code,
            "viewport-presentation",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.studio.presentation.avalonia",
                "viewport-presentation-transaction",
                new StudioDiagnosticScope(
                    "viewport-session",
                    context.SessionId.Value.ToString("D"),
                    checked((long)context.Epoch)),
                context.OperationId,
                context.CorrelationId,
                context.ParentCorrelationId),
            $"Required viewport presentation transition was {transition.Disposition}.",
            "Inspect the edge, phase, outcome, and participant scope before retrying.",
            attributes);
    }

    private static ImmutableArray<StudioDiagnosticAttribute> CreateAttributes(
        ViewportPresentationTransitionDiagnosticContext context,
        ViewportPresentationTransitionResult transition)
    {
        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(12);
        attributes.Add(new StudioDiagnosticAttribute(
            "edge",
            context.Edge.ToString()));
        attributes.Add(new StudioDiagnosticAttribute(
            "endpointId",
            context.EndpointId.Value));
        attributes.Add(new StudioDiagnosticAttribute(
            "epoch",
            context.Epoch.ToString(CultureInfo.InvariantCulture)));
        attributes.Add(new StudioDiagnosticAttribute(
            "transactionId",
            context.TransactionId.Value.ToString(CultureInfo.InvariantCulture)));
        attributes.Add(new StudioDiagnosticAttribute(
            "presentationGeneration",
            context.Generation.ToString(CultureInfo.InvariantCulture)));
        attributes.Add(new StudioDiagnosticAttribute(
            "phase",
            transition.Phase.ToString()));
        attributes.Add(new StudioDiagnosticAttribute(
            "transitionDisposition",
            transition.Disposition.ToString()));
        attributes.Add(new StudioDiagnosticAttribute(
            "outcomeKind",
            transition.TransactionOutcome.Kind.ToString()));
        if (context.ParticipantId is ViewportPresentationParticipantId participantId)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                "participantId",
                participantId.Value.ToString(CultureInfo.InvariantCulture)));
        }

        if (transition.TransactionOutcome.Failure is not ViewportPresentationFailure failure)
        {
            return attributes.ToImmutable();
        }

        attributes.Add(new StudioDiagnosticAttribute(
            "failureCode",
            failure.Code.ToString()));
        attributes.Add(new StudioDiagnosticAttribute(
            "failureDisposition",
            failure.Disposition.ToString()));
        if (failure.ParticipantId is ViewportPresentationParticipantId failureParticipantId)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                "failureParticipantId",
                failureParticipantId.Value.ToString(CultureInfo.InvariantCulture)));
        }
        return attributes.ToImmutable();
    }
}

internal sealed class ViewportPresentationTransitionDiagnosticSession
{
    private readonly object gate_ = new();
    private readonly HashSet<PublishedEdge> publishedEdges_ = [];
    private readonly IStudioDiagnosticHub diagnostics_;
    private readonly ViewportPresentationTransactionId transactionId_;
    private readonly Guid operationId_ = Guid.NewGuid();
    private readonly Guid correlationId_ = Guid.NewGuid();

    public ViewportPresentationTransitionDiagnosticSession(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationTransactionId transactionId)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        if (!transactionId.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation transaction id must be valid.",
                nameof(transactionId));
        }

        diagnostics_ = diagnostics;
        transactionId_ = transactionId;
    }

    public void RequireApplied(
        ViewportPresentationTransitionEdge edge,
        ViewportPresentationTransitionResult transition,
        ViewportPresentationTelemetryIdentity identity,
        ViewportPresentationParticipantId? participantId = null)
    {
        if (transition.Disposition is
            ViewportPresentationTransitionDisposition.Applied or
            ViewportPresentationTransitionDisposition.AlreadyApplied)
        {
            return;
        }
        if (!identity.IsValid || identity.TransactionId != transactionId_)
        {
            throw new ArgumentException(
                "Required-edge diagnostics must use an identity from the current transaction.",
                nameof(identity));
        }

        var key = new PublishedEdge(edge, identity.EndpointId, participantId);
        var shouldPublish = false;
        lock (gate_)
        {
            shouldPublish = publishedEdges_.Add(key);
        }

        if (shouldPublish)
        {
            var context = new ViewportPresentationTransitionDiagnosticContext(
                edge,
                identity.EndpointId,
                identity.SessionId,
                identity.Epoch,
                transactionId_,
                identity.Generation,
                operationId_,
                correlationId_,
                participantId: participantId);
            var diagnostic = ViewportPresentationTransitionDiagnosticProjector
                .ProjectRequiredEdgeFailure(context, transition);
            if (diagnostic is not null)
            {
                diagnostics_.PublishDiagnostic(diagnostic);
            }
        }

        throw new InvalidOperationException(
            $"Required viewport presentation transition {edge} was " +
            $"{transition.Disposition} at phase {transition.Phase}.");
    }

    private readonly record struct PublishedEdge(
        ViewportPresentationTransitionEdge Edge,
        ViewportPresentationEndpointId EndpointId,
        ViewportPresentationParticipantId? ParticipantId);
}

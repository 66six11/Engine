using System;
using System.Collections.Immutable;
using System.Globalization;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal static class ViewportPresentationStateDiagnosticProjector
{
    public static StudioDiagnosticWrite ProjectFailure(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportPresentationState state,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision,
        Guid operationId,
        Guid correlationId) =>
        Project(
            diagnostics,
            endpointId,
            state,
            sessionId,
            generation,
            revision,
            operationId,
            correlationId,
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            "studio.viewport.presentation.failed",
            "Viewport presentation entered a degraded state.",
            "Inspect the endpoint state and compositor/native compatibility before retrying.");

    public static StudioDiagnosticWrite ProjectRecovery(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision,
        Guid operationId,
        Guid correlationId) =>
        Project(
            diagnostics,
            endpointId,
            ViewportPresentationState.Ready,
            sessionId,
            generation,
            revision,
            operationId,
            correlationId,
            StudioDiagnosticSeverity.Info,
            StudioDiagnosticChannel.Debug,
            "studio.viewport.presentation.recovered",
            "Viewport presentation recovered and is ready.",
            "No action is required unless this viewport degrades again.");

    private static StudioDiagnosticWrite Project(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportPresentationState state,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision,
        Guid operationId,
        Guid correlationId,
        StudioDiagnosticSeverity severity,
        StudioDiagnosticChannel channel,
        string code,
        string message,
        string remediation)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        if (!endpointId.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation endpoint id must be valid.",
                nameof(endpointId));
        }
        if (!Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(nameof(state));
        }
        if (generation > long.MaxValue)
        {
            throw new ArgumentOutOfRangeException(
                nameof(generation),
                "Viewport presentation generation must fit the diagnostic scope generation.");
        }
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException("Operation id must not be empty.", nameof(operationId));
        }
        if (correlationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Correlation id must not be empty.",
                nameof(correlationId));
        }

        var scope = sessionId.IsValid
            ? new StudioDiagnosticScope(
                "viewport-session",
                sessionId.Value.ToString("D"),
                checked((long)generation))
            : StudioDiagnosticScope.Process(diagnostics.ProcessIdentity);
        return new StudioDiagnosticWrite(
            severity,
            channel,
            code,
            "viewport-presentation",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.studio.presentation.avalonia",
                "viewport-composition-control",
                scope,
                operationId,
                correlationId),
            message,
            remediation,
            ImmutableArray.Create(
                new StudioDiagnosticAttribute("endpointId", endpointId.Value),
                new StudioDiagnosticAttribute(
                    "generation",
                    generation.ToString(CultureInfo.InvariantCulture)),
                new StudioDiagnosticAttribute("state", state.ToString()),
                new StudioDiagnosticAttribute(
                    "revision",
                    revision.ToString(CultureInfo.InvariantCulture))));
    }
}

internal sealed class ViewportPresentationStateDiagnosticTracker
{
    private readonly IStudioDiagnosticHub diagnostics_;
    private readonly ViewportPresentationEndpointId endpointId_;
    private Episode? episode_;

    public ViewportPresentationStateDiagnosticTracker(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        if (!endpointId.IsValid)
        {
            throw new ArgumentException(
                "Viewport presentation endpoint id must be valid.",
                nameof(endpointId));
        }

        diagnostics_ = diagnostics;
        endpointId_ = endpointId;
    }

    public void ObserveDegraded(
        ViewportPresentationState state,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision)
    {
        if (!IsDegraded(state))
        {
            throw new ArgumentOutOfRangeException(
                nameof(state),
                state,
                "Only degraded viewport presentation states can begin an episode.");
        }
        if (episode_ is { } activeEpisode &&
            IsSameSessionScope(activeEpisode.SessionId, sessionId))
        {
            return;
        }

        var episode = new Episode(Guid.NewGuid(), Guid.NewGuid(), sessionId);
        diagnostics_.PublishDiagnostic(
            ViewportPresentationStateDiagnosticProjector.ProjectFailure(
                diagnostics_,
                endpointId_,
                state,
                sessionId,
                generation,
                revision,
                episode.OperationId,
                episode.CorrelationId));
        episode_ = episode;
    }

    public void ObserveStatus(
        ViewportPresentationState state,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision)
    {
        if (IsDegraded(state) || !Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(
                nameof(state),
                state,
                "A normal viewport presentation status cannot be degraded.");
        }

        if (episode_ is null)
        {
            return;
        }
        if (!IsSameSessionScope(episode_.SessionId, sessionId))
        {
            // A late state edge from a replaced session cannot close or recover the
            // active session's failure episode.
            return;
        }
        if (state == ViewportPresentationState.Probing)
        {
            return;
        }
        if (state == ViewportPresentationState.Ready)
        {
            var episode = episode_;
            diagnostics_.PublishDiagnostic(
                ViewportPresentationStateDiagnosticProjector.ProjectRecovery(
                    diagnostics_,
                    endpointId_,
                    sessionId,
                    generation,
                    revision,
                    episode.OperationId,
                    episode.CorrelationId));
        }

        episode_ = null;
    }

    private static bool IsDegraded(ViewportPresentationState state) => state is
        ViewportPresentationState.Unsupported or
        ViewportPresentationState.NativeUnavailable or
        ViewportPresentationState.DeviceMismatch or
        ViewportPresentationState.RenderFailed;

    private static bool IsSameSessionScope(
        ViewportSessionId previous,
        ViewportSessionId current) =>
        !previous.IsValid || !current.IsValid || previous == current;

    private sealed record Episode(
        Guid OperationId,
        Guid CorrelationId,
        ViewportSessionId SessionId);
}

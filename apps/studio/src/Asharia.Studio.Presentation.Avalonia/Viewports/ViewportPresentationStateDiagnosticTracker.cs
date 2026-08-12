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
        Guid correlationId,
        StudioProblemId problemId) =>
        Project(
            diagnostics,
            endpointId,
            state,
            sessionId,
            generation,
            generation,
            revision,
            operationId,
            correlationId,
            StudioDiagnosticSeverity.Error,
            "studio.viewport.presentation.failed",
            "Viewport presentation entered a degraded state.",
            "Inspect the endpoint state and compositor/native compatibility before retrying.",
            problemId,
            StudioProblemTransition.Active);

    public static StudioDiagnosticWrite ProjectRecovery(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportSessionId sessionId,
        ulong generation,
        ulong revision,
        Guid operationId,
        Guid correlationId,
        StudioProblemId problemId) =>
        Project(
            diagnostics,
            endpointId,
            ViewportPresentationState.Ready,
            sessionId,
            generation,
            generation,
            revision,
            operationId,
            correlationId,
            StudioDiagnosticSeverity.Info,
            "studio.viewport.presentation.recovered",
            "Viewport presentation recovered and is ready.",
            "No action is required unless this viewport degrades again.",
            problemId,
            StudioProblemTransition.Resolved);

    public static StudioDiagnosticWrite ProjectStale(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportPresentationState observedState,
        ViewportSessionId ownerSessionId,
        ulong ownerGeneration,
        ulong observedGeneration,
        ulong observedRevision,
        Guid operationId,
        Guid correlationId,
        StudioProblemId problemId,
        string closureReason) =>
        Project(
            diagnostics,
            endpointId,
            observedState,
            ownerSessionId,
            ownerGeneration,
            observedGeneration,
            observedRevision,
            operationId,
            correlationId,
            StudioDiagnosticSeverity.Info,
            "studio.viewport.presentation.stale",
            "Viewport presentation problem no longer applies to the active viewport scope.",
            "No action is required unless the current viewport scope reports another failure.",
            problemId,
            StudioProblemTransition.Stale,
            closureReason);

    private static StudioDiagnosticWrite Project(
        IStudioDiagnosticHub diagnostics,
        ViewportPresentationEndpointId endpointId,
        ViewportPresentationState state,
        ViewportSessionId scopeSessionId,
        ulong scopeGeneration,
        ulong observedGeneration,
        ulong revision,
        Guid operationId,
        Guid correlationId,
        StudioDiagnosticSeverity severity,
        string code,
        string message,
        string remediation,
        StudioProblemId problemId,
        StudioProblemTransition problemTransition,
        string? closureReason = null)
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
        if (scopeGeneration > long.MaxValue)
        {
            throw new ArgumentOutOfRangeException(
                nameof(scopeGeneration),
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
        if (string.IsNullOrWhiteSpace(problemId.Value))
        {
            throw new ArgumentException("Problem id must be valid.", nameof(problemId));
        }
        if (!Enum.IsDefined(problemTransition))
        {
            throw new ArgumentOutOfRangeException(nameof(problemTransition));
        }
        if (closureReason is not null && string.IsNullOrWhiteSpace(closureReason))
        {
            throw new ArgumentException(
                "Closure reason must be valid when supplied.",
                nameof(closureReason));
        }

        var scope = scopeSessionId.IsValid
            ? new StudioDiagnosticScope(
                "viewport-session",
                scopeSessionId.Value.ToString("D"),
                checked((long)scopeGeneration))
            : StudioDiagnosticScope.Process(diagnostics.ProcessIdentity);
        var attributes = ImmutableArray.Create(
            new StudioDiagnosticAttribute("endpointId", endpointId.Value),
            new StudioDiagnosticAttribute(
                "generation",
                observedGeneration.ToString(CultureInfo.InvariantCulture)),
            new StudioDiagnosticAttribute("state", state.ToString()),
            new StudioDiagnosticAttribute(
                "revision",
                revision.ToString(CultureInfo.InvariantCulture)));
        if (closureReason is not null)
        {
            attributes = attributes.Add(
                new StudioDiagnosticAttribute("closureReason", closureReason));
        }

        return new StudioDiagnosticWrite(
            severity,
            StudioDiagnosticChannel.Problem,
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
            attributes,
            problemId,
            problemTransition);
    }
}

internal sealed class ViewportPresentationStateDiagnosticTracker
{
    private const string ProblemIdPrefix = "viewport-presentation:";
    private const string SessionReplacedClosureReason = "session-replaced";
    private const string StateBoundaryClosureReason = "state-boundary";

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
        var activeEpisode = episode_;
        if (activeEpisode is not null &&
            IsSameSessionScope(activeEpisode.SessionId, sessionId))
        {
            return;
        }

        if (activeEpisode is not null)
        {
            diagnostics_.PublishDiagnostic(
                ViewportPresentationStateDiagnosticProjector.ProjectStale(
                    diagnostics_,
                    endpointId_,
                    state,
                    activeEpisode.SessionId,
                    activeEpisode.Generation,
                    generation,
                    revision,
                    activeEpisode.OperationId,
                    activeEpisode.CorrelationId,
                    activeEpisode.ProblemId,
                    SessionReplacedClosureReason));
            episode_ = null;
        }

        var episode = new Episode(
            Guid.NewGuid(),
            Guid.NewGuid(),
            new StudioProblemId(ProblemIdPrefix + Guid.NewGuid().ToString("N")),
            sessionId,
            generation);
        diagnostics_.PublishDiagnostic(
            ViewportPresentationStateDiagnosticProjector.ProjectFailure(
                diagnostics_,
                endpointId_,
                state,
                sessionId,
                generation,
                revision,
                episode.OperationId,
                episode.CorrelationId,
                episode.ProblemId));
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

        var episode = episode_;
        if (episode is null)
        {
            return;
        }
        if (!IsSameSessionScope(episode.SessionId, sessionId))
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
            diagnostics_.PublishDiagnostic(
                ViewportPresentationStateDiagnosticProjector.ProjectRecovery(
                    diagnostics_,
                    endpointId_,
                    sessionId,
                    generation,
                    revision,
                    episode.OperationId,
                    episode.CorrelationId,
                    episode.ProblemId));
        }
        else
        {
            diagnostics_.PublishDiagnostic(
                ViewportPresentationStateDiagnosticProjector.ProjectStale(
                    diagnostics_,
                    endpointId_,
                    state,
                    episode.SessionId,
                    episode.Generation,
                    generation,
                    revision,
                    episode.OperationId,
                    episode.CorrelationId,
                    episode.ProblemId,
                    StateBoundaryClosureReason));
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
        StudioProblemId ProblemId,
        ViewportSessionId SessionId,
        ulong Generation);
}

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Actions;

public sealed class StudioActionExecutor
{
    private readonly StudioActionRegistry registry_;

    public StudioActionExecutor(StudioActionRegistry registry)
    {
        ArgumentNullException.ThrowIfNull(registry);
        registry_ = registry;
    }

    public StudioActionStateEvaluation EvaluateState(
        StudioActionId actionId,
        StudioActionContextSnapshot context)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }
        ArgumentNullException.ThrowIfNull(context);

        if (!registry_.TryGetRuntimeEntry(actionId, out var entry) || entry is null)
        {
            return StudioActionStateEvaluation.Unknown(actionId);
        }

        try
        {
            var state = entry.StateEvaluator(context);
            return state is null
                ? StudioActionStateEvaluation.Failed(
                    actionId,
                    $"Action '{actionId}' returned no state.")
                : StudioActionStateEvaluation.Evaluated(actionId, state);
        }
        catch (Exception exception)
        {
            return StudioActionStateEvaluation.Failed(
                actionId,
                $"Action '{actionId}' state evaluation failed: {exception.Message}");
        }
    }

    public async ValueTask<StudioActionResult> ExecuteAsync(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        CancellationToken cancellationToken = default)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }
        ArgumentNullException.ThrowIfNull(context);

        if (!registry_.TryGetRuntimeEntry(actionId, out var entry) || entry is null)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Unknown,
                $"Action '{actionId}' is not registered.");
        }

        StudioActionState state;
        try
        {
            state = entry.StateEvaluator(context) ?? throw new InvalidOperationException(
                $"Action '{actionId}' returned no state.");
        }
        catch (Exception exception)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Failed,
                $"Action '{actionId}' state evaluation failed: {exception.Message}");
        }

        if (!state.IsVisible)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Disabled,
                state.DisabledReason ?? "Action is not visible in this context.");
        }
        if (!state.IsEnabled)
        {
            return Result(
                actionId,
                context,
                MapBlockKind(state.BlockKind),
                state.DisabledReason!);
        }
        if (cancellationToken.IsCancellationRequested)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Cancelled,
                "Action execution was cancelled before it started.");
        }

        StudioActionCompletion completion;
        try
        {
            completion = await entry.Handler(context, cancellationToken);
            if (completion is null)
            {
                return Result(
                    actionId,
                    context,
                    StudioActionResultStatus.Failed,
                    $"Action '{actionId}' returned no completion result.");
            }
        }
        catch (OperationCanceledException)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Cancelled,
                "Action execution was cancelled.");
        }
        catch (Exception exception)
        {
            return Result(
                actionId,
                context,
                StudioActionResultStatus.Failed,
                $"Action '{actionId}' execution failed: {exception.Message}");
        }

        return Result(
            actionId,
            context,
            completion.Status,
            completion.Message,
            completion.DiagnosticSequence,
            completion.ProjectEditId);
    }

    private static StudioActionResultStatus MapBlockKind(
        StudioActionBlockKind blockKind) => blockKind switch
        {
            StudioActionBlockKind.Disabled => StudioActionResultStatus.Disabled,
            StudioActionBlockKind.Stale => StudioActionResultStatus.Stale,
            StudioActionBlockKind.Conflict => StudioActionResultStatus.Conflict,
            _ => throw new ArgumentOutOfRangeException(nameof(blockKind), blockKind, null),
        };

    private static StudioActionResult Result(
        StudioActionId actionId,
        StudioActionContextSnapshot context,
        StudioActionResultStatus status,
        string message,
        long? diagnosticSequence = null,
        Projects.ProjectEditId? projectEditId = null) =>
        new(
            actionId,
            status,
            context.OperationId,
            context.CorrelationId,
            context.ParentCorrelationId,
            message,
            diagnosticSequence,
            projectEditId);
}

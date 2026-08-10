using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal enum ViewportPresentationParticipantHookPoint
{
    BeforePrepare,
    AfterPrepared,
}

internal enum ViewportPresentationGroupHookPoint
{
    BeforePublish,
    BeforeFinalize,
}

internal enum ViewportPresentationRollbackHookPoint
{
    BeforeLayoutRollback,
}

internal readonly record struct ViewportPresentationTransactionHookContext(
    ulong TransactionId,
    int ParticipantIndex,
    string ParticipantId,
    ViewportPresentationTelemetryIdentity Identity);

internal sealed record ViewportPresentationTransactionGroupHookContext(
    ulong TransactionId,
    IReadOnlyList<ViewportPresentationTransactionHookContext> Participants);

/// <summary>
/// Process-smoke-only scheduling and fault seam. Production construction never supplies hooks.
/// It deliberately wraps coordinator boundaries without changing endpoint resource ownership.
/// </summary>
internal sealed class ViewportPresentationTransactionTestHooks
{
    public Func<
        ViewportPresentationParticipantHookPoint,
        ViewportPresentationTransactionHookContext,
        CancellationToken,
        ValueTask>? BeforeParticipantAsync { get; init; }

    public Func<
        ViewportPresentationGroupHookPoint,
        ViewportPresentationTransactionGroupHookContext,
        CancellationToken,
        ValueTask>? BeforeGroupAsync { get; init; }

    public Func<
        Task,
        ViewportPresentationTransactionGroupHookContext,
        Task>? WrapGroupRendered { get; init; }

    public Action<
        ViewportPresentationRollbackHookPoint,
        ViewportPresentationTransactionGroupHookContext>? AtRollback { get; init; }

    public ValueTask BeforeParticipantStageAsync(
        ViewportPresentationParticipantHookPoint point,
        ViewportPresentationTransactionHookContext context,
        CancellationToken cancellationToken) =>
        BeforeParticipantAsync?.Invoke(point, context, cancellationToken) ??
        ValueTask.CompletedTask;

    public ValueTask BeforeGroupStageAsync(
        ViewportPresentationGroupHookPoint point,
        ViewportPresentationTransactionGroupHookContext context,
        CancellationToken cancellationToken) =>
        BeforeGroupAsync?.Invoke(point, context, cancellationToken) ??
        ValueTask.CompletedTask;

    public Task WrapRenderedTask(
        Task rendered,
        ViewportPresentationTransactionGroupHookContext context)
    {
        ArgumentNullException.ThrowIfNull(rendered);
        ArgumentNullException.ThrowIfNull(context);
        return WrapGroupRendered?.Invoke(rendered, context) ?? rendered;
    }

    public void AtRollbackStage(
        ViewportPresentationRollbackHookPoint point,
        ViewportPresentationTransactionGroupHookContext context) =>
        AtRollback?.Invoke(point, context);
}

using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Layout;

namespace Editor.Shell.Docking.Splitters;

internal readonly record struct EditorDockSplitResizeCoordinatorMetrics(
    ulong AcceptedRequests,
    ulong ProcessedRequests,
    ulong QueuedSupersededRequests,
    ulong ActiveCancelledRequests,
    int MaximumPendingWork,
    bool HasActive,
    bool HasQueued);

internal readonly record struct EditorDockSplitResizeDefinition(
    GridLength UserLength,
    double ActualLength,
    double MinimumLength,
    double MaximumLength);

internal readonly record struct EditorDockSplitResizePolicyInput(
    EditorDockSplitResizeDefinition First,
    EditorDockSplitResizeDefinition Second,
    double OriginalCombinedActualLength,
    double RequestedDelta,
    bool UseLayoutRounding,
    double LayoutScale);

internal readonly record struct EditorDockSplitResizeProposal(
    GridLength FirstLength,
    GridLength SecondLength,
    double FirstActualLength,
    double SecondActualLength,
    double AppliedDelta,
    double MinimumDelta,
    double MaximumDelta);

internal readonly record struct EditorDockSplitResizeCommittedSnapshot(
    string SplitId,
    Orientation Orientation,
    GridLength FirstLength,
    GridLength SecondLength,
    double FirstActualLength,
    double SecondActualLength,
    double LayoutScale);

internal readonly record struct EditorDockSplitResizeRequest(
    ulong TransactionId,
    ulong Sequence,
    EditorDockSplitResizeCommittedSnapshot Committed,
    EditorDockSplitResizeProposal Requested,
    double CumulativeDelta,
    bool IsFinal);

internal readonly record struct EditorDockSplitResizeCancellation(
    ulong TransactionId,
    ulong LastSequence,
    string SplitId);

internal static class EditorDockSplitResizePolicy
{
    public static bool TryResolve(
        EditorDockSplitResizePolicyInput input,
        out EditorDockSplitResizeProposal proposal)
    {
        proposal = default;
        if (!IsValidDefinition(input.First)
            || !IsValidDefinition(input.Second)
            || !double.IsFinite(input.OriginalCombinedActualLength)
            || input.OriginalCombinedActualLength < 0d
            || !double.IsFinite(input.RequestedDelta)
            || !double.IsFinite(input.LayoutScale)
            || input.LayoutScale <= 0d)
        {
            return false;
        }

        var firstIsStar = input.First.UserLength.IsStar;
        var secondIsStar = input.Second.UserLength.IsStar;
        var combinedActualLength = input.First.ActualLength + input.Second.ActualLength;
        if (!double.IsFinite(combinedActualLength))
        {
            return false;
        }

        if (firstIsStar && secondIsStar)
        {
            var tolerance = (1d / input.LayoutScale) + LayoutHelper.LayoutEpsilon;
            if (Math.Abs(combinedActualLength - input.OriginalCombinedActualLength) > tolerance)
            {
                return false;
            }
        }

        var delta = input.UseLayoutRounding
            ? LayoutHelper.RoundLayoutValue(input.RequestedDelta, input.LayoutScale)
            : input.RequestedDelta;
        var minimumDelta = -Math.Min(
            input.First.ActualLength - input.First.MinimumLength,
            input.Second.MaximumLength - input.Second.ActualLength);
        var maximumDelta = Math.Min(
            input.First.MaximumLength - input.First.ActualLength,
            input.Second.ActualLength - input.Second.MinimumLength);
        delta = Math.Min(Math.Max(delta, minimumDelta), maximumDelta);

        var firstActualLength = input.First.ActualLength + delta;
        var secondActualLength = combinedActualLength - firstActualLength;
        if (!double.IsFinite(delta)
            || !double.IsFinite(firstActualLength)
            || !double.IsFinite(secondActualLength))
        {
            return false;
        }

        GridLength firstLength;
        GridLength secondLength;
        if (firstIsStar && secondIsStar)
        {
            firstLength = new GridLength(firstActualLength, GridUnitType.Star);
            secondLength = new GridLength(secondActualLength, GridUnitType.Star);
        }
        else if (!firstIsStar)
        {
            firstLength = new GridLength(firstActualLength);
            secondLength = input.Second.UserLength;
        }
        else
        {
            firstLength = input.First.UserLength;
            secondLength = new GridLength(secondActualLength);
        }

        proposal = new EditorDockSplitResizeProposal(
            firstLength,
            secondLength,
            firstActualLength,
            secondActualLength,
            delta,
            minimumDelta,
            maximumDelta);
        return true;
    }

    private static bool IsValidDefinition(EditorDockSplitResizeDefinition definition)
    {
        return double.IsFinite(definition.ActualLength)
            && definition.ActualLength >= 0d
            && double.IsFinite(definition.MinimumLength)
            && definition.MinimumLength >= 0d
            && !double.IsNaN(definition.MaximumLength)
            && definition.MaximumLength >= definition.MinimumLength;
    }
}

internal sealed class EditorDockSplitResizeCoordinator : IDisposable
{
    private readonly object gate_ = new();
    private readonly Func<EditorDockSplitResizeRequest, CancellationToken, Task> processLatestAsync_;
    private readonly Action<EditorDockSplitResizeCancellation> cancel_;
    private readonly Action<Exception> reportFailure_;
    private EditorDockSplitResizeRequest? currentRequest_;
    private EditorDockSplitResizeRequest? queuedRequest_;
    private ActiveRequest? activeRequest_;
    private TaskCompletionSource idleCompletion_ = CreateCompletedIdleCompletion();
    private ulong lastTransactionId_;
    private ulong lastAcceptedSequence_;
    private ulong acceptedRequests_;
    private ulong processedRequests_;
    private ulong queuedSupersededRequests_;
    private ulong activeCancelledRequests_;
    private int maximumPendingWork_;
    private bool isDrainRunning_;
    private bool isDisposed_;

    public EditorDockSplitResizeCoordinator(
        Func<EditorDockSplitResizeRequest, CancellationToken, Task> processLatestAsync,
        Action<EditorDockSplitResizeCancellation> cancel,
        Action<Exception>? reportFailure = null)
    {
        processLatestAsync_ = processLatestAsync
            ?? throw new ArgumentNullException(nameof(processLatestAsync));
        cancel_ = cancel ?? throw new ArgumentNullException(nameof(cancel));
        reportFailure_ = reportFailure ?? (static _ => { });
    }

    public static EditorDockSplitResizeCoordinator CreateDiscarding()
    {
        return new EditorDockSplitResizeCoordinator(
            static (_, _) => Task.CompletedTask,
            static _ => { });
    }

    public bool RequestLatest(EditorDockSplitResizeRequest request)
    {
        ActiveRequest? active;
        EditorDockSplitResizeRequest? queued;
        var startDrain = false;
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            if (request.TransactionId < lastTransactionId_
                || request.TransactionId == lastTransactionId_
                    && request.Sequence <= lastAcceptedSequence_)
            {
                return false;
            }

            lastTransactionId_ = request.TransactionId;
            lastAcceptedSequence_ = request.Sequence;
            acceptedRequests_ = checked(acceptedRequests_ + 1);
            active = activeRequest_;
            queued = queuedRequest_;
            currentRequest_ = request;
            queuedRequest_ = request;
            maximumPendingWork_ = Math.Max(
                maximumPendingWork_,
                (activeRequest_ is null ? 0 : 1) + 1);
            if (queued is not null)
            {
                queuedSupersededRequests_ = checked(queuedSupersededRequests_ + 1);
            }
            if (!isDrainRunning_)
            {
                isDrainRunning_ = true;
                idleCompletion_ = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                startDrain = true;
            }
        }

        if (active is { } activeRequest &&
            activeRequest.Request.TransactionId != request.TransactionId)
        {
            CancelActive(activeRequest);
        }
        if (queued is { } supersededQueued)
        {
            NotifyCancellation(supersededQueued);
        }
        if (startDrain)
        {
            _ = DrainAsync();
        }

        return true;
    }

    public bool IsCurrent(ulong transactionId, ulong sequence)
    {
        lock (gate_)
        {
            return currentRequest_ is { } current
                && current.TransactionId == transactionId
                && current.Sequence == sequence;
        }
    }

    public bool TryComplete(ulong transactionId, ulong sequence)
    {
        lock (gate_)
        {
            if (currentRequest_ is not { } current
                || current.TransactionId != transactionId
                || current.Sequence != sequence)
            {
                return false;
            }

            currentRequest_ = null;
            if (queuedRequest_ is { } queued
                && queued.TransactionId == transactionId
                && queued.Sequence == sequence)
            {
                queuedRequest_ = null;
            }

            return true;
        }
    }

    public Task WhenIdleAsync()
    {
        lock (gate_)
        {
            return idleCompletion_.Task;
        }
    }

    public EditorDockSplitResizeCoordinatorMetrics CaptureMetrics()
    {
        lock (gate_)
        {
            return new EditorDockSplitResizeCoordinatorMetrics(
                acceptedRequests_,
                processedRequests_,
                queuedSupersededRequests_,
                activeCancelledRequests_,
                maximumPendingWork_,
                activeRequest_ is not null,
                queuedRequest_ is not null);
        }
    }

    public bool Cancel(EditorDockSplitResizeCancellation cancellation)
    {
        ActiveRequest? active;
        EditorDockSplitResizeRequest? queued;
        lock (gate_)
        {
            if (currentRequest_ is not { } current
                || current.TransactionId != cancellation.TransactionId
                || current.Sequence > cancellation.LastSequence)
            {
                return false;
            }

            currentRequest_ = null;
            active = activeRequest_ is { } activeCandidate
                && activeCandidate.Request.TransactionId == cancellation.TransactionId
                && activeCandidate.Request.Sequence <= cancellation.LastSequence
                    ? activeCandidate
                    : null;
            queued = queuedRequest_ is { } queuedCandidate
                && queuedCandidate.TransactionId == cancellation.TransactionId
                && queuedCandidate.Sequence <= cancellation.LastSequence
                    ? queuedCandidate
                    : null;
            if (queued is not null)
            {
                queuedRequest_ = null;
            }
        }

        CancelActive(active);
        if (queued is { } canceledQueued)
        {
            NotifyCancellation(canceledQueued);
        }
        return true;
    }

    public void Dispose()
    {
        ActiveRequest? active;
        EditorDockSplitResizeRequest? queued;
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            isDisposed_ = true;
            currentRequest_ = null;
            active = activeRequest_;
            queued = queuedRequest_;
            queuedRequest_ = null;
        }

        CancelActive(active);
        if (queued is { } canceledQueued)
        {
            NotifyCancellation(canceledQueued);
        }
    }

    private async Task DrainAsync()
    {
        while (true)
        {
            ActiveRequest active;
            lock (gate_)
            {
                if (queuedRequest_ is not { } request)
                {
                    isDrainRunning_ = false;
                    idleCompletion_.TrySetResult();
                    return;
                }

                queuedRequest_ = null;
                active = new ActiveRequest(request, new CancellationTokenSource());
                activeRequest_ = active;
                processedRequests_ = checked(processedRequests_ + 1);
            }

            try
            {
                await processLatestAsync_(active.Request, active.Cancellation.Token);
            }
            catch (OperationCanceledException) when (active.Cancellation.IsCancellationRequested)
            {
            }
            catch (Exception exception)
            {
                reportFailure_(exception);
            }
            finally
            {
                lock (gate_)
                {
                    if (ReferenceEquals(activeRequest_, active))
                    {
                        activeRequest_ = null;
                    }
                    if (currentRequest_ is { } current
                        && current.TransactionId == active.Request.TransactionId
                        && current.Sequence == active.Request.Sequence)
                    {
                        currentRequest_ = null;
                    }
                }

                active.Cancellation.Dispose();
            }
        }
    }

    private void CancelActive(ActiveRequest? active)
    {
        if (active is null || active.Cancellation.IsCancellationRequested)
        {
            return;
        }

        active.Cancellation.Cancel();
        lock (gate_)
        {
            activeCancelledRequests_ = checked(activeCancelledRequests_ + 1);
        }
        NotifyCancellation(active.Request);
    }

    private void NotifyCancellation(EditorDockSplitResizeRequest request)
    {
        cancel_(new EditorDockSplitResizeCancellation(
            request.TransactionId,
            request.Sequence,
            request.Committed.SplitId));
    }

    private static TaskCompletionSource CreateCompletedIdleCompletion()
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        completion.SetResult();
        return completion;
    }

    private sealed record ActiveRequest(
        EditorDockSplitResizeRequest Request,
        CancellationTokenSource Cancellation);
}

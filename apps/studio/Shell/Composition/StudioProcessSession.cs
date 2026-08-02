using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;

namespace Editor.Shell.Composition;

internal enum StudioProcessSessionState
{
    Created,
    Starting,
    Running,
    Stopping,
    Stopped,
    Faulted,
}

internal enum StudioProcessStopStatus
{
    Completed,
    TimedOut,
    Faulted,
}

internal enum StudioCompositionTeardownStatus
{
    NotCreated,
    LifetimeCancellationTimedOut,
    Disposed,
    LifecycleGateTimedOut,
    DisposeTimedOut,
    DisposeFaulted,
}

internal sealed record StudioTeardownFailure(string Code, string Message);

internal sealed record StudioTeardownReceipt(
    StudioProcessIdentity SessionId,
    StudioProcessStopStatus Status,
    DateTimeOffset StartedAtUtc,
    DateTimeOffset CompletedAtUtc,
    StudioCompositionTeardownStatus CompositionStatus,
    ImmutableArray<StudioTeardownFailure> Failures);

internal sealed class StudioProcessSession
{
    private readonly Func<CancellationToken, ValueTask<IAsyncDisposable>>
        createCompositionSession_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly SemaphoreSlim lifecycleGate_ = new(1, 1);
    private readonly object stateGate_ = new();
    private readonly object stopGate_ = new();
    private IAsyncDisposable? compositionSession_;
    private Task<StudioTeardownReceipt>? stopTask_;
    private StudioProcessSessionState state_ = StudioProcessSessionState.Created;

    public StudioProcessSession(
        Func<CancellationToken, ValueTask<IAsyncDisposable>> createCompositionSession,
        StudioProcessIdentity? sessionId = null)
    {
        ArgumentNullException.ThrowIfNull(createCompositionSession);

        createCompositionSession_ = createCompositionSession;
        SessionId = sessionId ?? StudioProcessIdentity.CreateNew();
    }

    public StudioProcessIdentity SessionId { get; }

    public StudioProcessSessionState State
    {
        get
        {
            lock (stateGate_)
            {
                return state_;
            }
        }
    }

    public async ValueTask StartAsync(
        CancellationToken cancellationToken = default)
    {
        await lifecycleGate_.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            if (State != StudioProcessSessionState.Created)
            {
                throw new InvalidOperationException(
                    $"Studio process session cannot start from state '{State}'.");
            }

            SetState(StudioProcessSessionState.Starting);
            using var startCancellation = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                lifetimeCancellation_.Token);
            IAsyncDisposable? createdSession = null;
            try
            {
                createdSession = await createCompositionSession_(startCancellation.Token)
                    .ConfigureAwait(true)
                    ?? throw new InvalidOperationException(
                        "Studio composition factory returned no owned session.");
                startCancellation.Token.ThrowIfCancellationRequested();
                compositionSession_ = createdSession;
                SetState(StudioProcessSessionState.Running);
            }
            catch (Exception startException)
            {
                var cleanupException = await DisposeCreatedSessionAsync(createdSession)
                    .ConfigureAwait(true);
                SetStateAfterStartFailure(startException);
                if (cleanupException is not null)
                {
                    throw new AggregateException(startException, cleanupException);
                }

                throw;
            }
        }
        finally
        {
            lifecycleGate_.Release();
        }
    }

    public ValueTask<StudioTeardownReceipt> StopAsync(
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (timeout < TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        Task<StudioTeardownReceipt> stopTask;
        lock (stopGate_)
        {
            stopTask_ ??= StopCoreAsync(timeout);
            stopTask = stopTask_;
        }

        return new ValueTask<StudioTeardownReceipt>(
            stopTask.WaitAsync(cancellationToken));
    }

    private async Task<StudioTeardownReceipt> StopCoreAsync(TimeSpan timeout)
    {
        var startedAtUtc = DateTimeOffset.UtcNow;
        var startedTimestamp = Stopwatch.GetTimestamp();
        var failures = ImmutableArray.CreateBuilder<StudioTeardownFailure>();
        SetState(StudioProcessSessionState.Stopping);
        try
        {
            var cancellationTask = lifetimeCancellation_.CancelAsync();
            if (!await WaitForCompletionAsync(
                    cancellationTask,
                    startedTimestamp,
                    timeout)
                .ConfigureAwait(true))
            {
                return new StudioTeardownReceipt(
                    SessionId,
                    StudioProcessStopStatus.TimedOut,
                    startedAtUtc,
                    DateTimeOffset.UtcNow,
                    StudioCompositionTeardownStatus.LifetimeCancellationTimedOut,
                    Failures: failures.ToImmutable());
            }
        }
        catch (Exception exception)
        {
            AddFailure(
                failures,
                "studio.teardown.lifetime-cancel.failed",
                exception);
        }

        var gateAcquired = await lifecycleGate_.WaitAsync(
                GetRemainingTime(startedTimestamp, timeout))
            .ConfigureAwait(true);
        if (!gateAcquired)
        {
            return new StudioTeardownReceipt(
                SessionId,
                StudioProcessStopStatus.TimedOut,
                startedAtUtc,
                DateTimeOffset.UtcNow,
                StudioCompositionTeardownStatus.LifecycleGateTimedOut,
                Failures: failures.ToImmutable());
        }

        IAsyncDisposable? session;
        try
        {
            SetState(StudioProcessSessionState.Stopping);
            session = compositionSession_;
            compositionSession_ = null;
        }
        finally
        {
            lifecycleGate_.Release();
        }

        var compositionStatus = StudioCompositionTeardownStatus.NotCreated;

        if (session is not null)
        {
            try
            {
                var disposeTask = session.DisposeAsync().AsTask();
                compositionStatus = await WaitForCompletionAsync(
                        disposeTask,
                        startedTimestamp,
                        timeout)
                    .ConfigureAwait(true)
                    ? StudioCompositionTeardownStatus.Disposed
                    : StudioCompositionTeardownStatus.DisposeTimedOut;
            }
            catch (Exception exception)
            {
                compositionStatus = StudioCompositionTeardownStatus.DisposeFaulted;
                AddFailure(
                    failures,
                    "studio.teardown.managed-session.failed",
                    exception);
            }
        }

        var status = compositionStatus == StudioCompositionTeardownStatus.DisposeTimedOut
            ? StudioProcessStopStatus.TimedOut
            : failures.Count > 0
                ? StudioProcessStopStatus.Faulted
                : StudioProcessStopStatus.Completed;
        SetState(status == StudioProcessStopStatus.Completed
            ? StudioProcessSessionState.Stopped
            : status == StudioProcessStopStatus.Faulted
                ? StudioProcessSessionState.Faulted
                : StudioProcessSessionState.Stopping);
        return new StudioTeardownReceipt(
            SessionId,
            status,
            startedAtUtc,
            DateTimeOffset.UtcNow,
            compositionStatus,
            Failures: failures.ToImmutable());
    }

    private static async ValueTask<Exception?> DisposeCreatedSessionAsync(
        IAsyncDisposable? session)
    {
        if (session is null)
        {
            return null;
        }

        try
        {
            await session.DisposeAsync().ConfigureAwait(true);
            return null;
        }
        catch (Exception exception)
        {
            return exception;
        }
    }

    private static async Task<bool> WaitForCompletionAsync(
        Task task,
        long startedTimestamp,
        TimeSpan timeout)
    {
        if (task.IsCompleted)
        {
            await task.ConfigureAwait(true);
            return true;
        }

        var remaining = GetRemainingTime(startedTimestamp, timeout);
        if (remaining == Timeout.InfiniteTimeSpan)
        {
            await task.ConfigureAwait(true);
            return true;
        }

        if (remaining == TimeSpan.Zero)
        {
            _ = ObserveAfterDeadlineAsync(task);
            return false;
        }

        using var timeoutCancellation = new CancellationTokenSource();
        var timeoutTask = Task.Delay(remaining, timeoutCancellation.Token);
        var completedTask = await Task.WhenAny(task, timeoutTask).ConfigureAwait(true);
        if (!ReferenceEquals(completedTask, task))
        {
            _ = ObserveAfterDeadlineAsync(task);
            return false;
        }

        await timeoutCancellation.CancelAsync().ConfigureAwait(true);
        await task.ConfigureAwait(true);
        return true;
    }

    private static async Task ObserveAfterDeadlineAsync(Task task)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (Exception)
        {
            // The immutable receipt already ended at the owner deadline. This
            // continuation only observes a late fault so it cannot escape.
        }
    }

    private static TimeSpan GetRemainingTime(
        long startedTimestamp,
        TimeSpan timeout)
    {
        if (timeout == Timeout.InfiniteTimeSpan)
        {
            return Timeout.InfiniteTimeSpan;
        }

        var elapsed = Stopwatch.GetElapsedTime(startedTimestamp);
        return elapsed >= timeout ? TimeSpan.Zero : timeout - elapsed;
    }

    private static void AddFailure(
        ImmutableArray<StudioTeardownFailure>.Builder failures,
        string code,
        Exception exception)
    {
        failures.Add(new StudioTeardownFailure(code, exception.Message));
    }

    private void SetState(StudioProcessSessionState state)
    {
        lock (stateGate_)
        {
            state_ = state;
        }
    }

    private void SetStateAfterStartFailure(Exception startException)
    {
        lock (stateGate_)
        {
            if (state_ == StudioProcessSessionState.Stopping)
            {
                return;
            }

            state_ = startException is OperationCanceledException
                ? StudioProcessSessionState.Stopped
                : StudioProcessSessionState.Faulted;
        }
    }
}

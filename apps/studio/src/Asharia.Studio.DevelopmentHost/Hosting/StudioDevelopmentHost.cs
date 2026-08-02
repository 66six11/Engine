using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Hosting;

public enum StudioDevelopmentHostState
{
    Running,
    Stopping,
    Stopped,
}

public enum StudioDevelopmentHostStopStatus
{
    Completed,
    TimedOut,
}

public sealed record StudioDevelopmentHostTeardownReceipt(
    StudioDevelopmentHostStopStatus Status,
    DateTimeOffset StartedAtUtc,
    DateTimeOffset CompletedAtUtc);

public sealed class StudioDevelopmentHost : IAsyncDisposable
{
    private static readonly TimeSpan DefaultDisposeTimeout = TimeSpan.FromSeconds(5);

    private readonly DevelopmentObservationSession session_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly SemaphoreSlim dispatchGate_ = new(1, 1);
    private readonly object stateGate_ = new();
    private readonly object stopGate_ = new();
    private Task<StudioDevelopmentHostTeardownReceipt>? stopTask_;
    private StudioDevelopmentHostState state_ = StudioDevelopmentHostState.Running;

    private StudioDevelopmentHost(DevelopmentObservationSession session)
    {
        session_ = session;
    }

    public StudioDevelopmentHostState State
    {
        get
        {
            lock (stateGate_)
            {
                return state_;
            }
        }
    }

    public StudioInstanceId StudioInstanceId => session_.StudioInstanceId;

    public StudioSessionId StudioSessionId => session_.StudioSessionId;

    public long EndpointGeneration => session_.EndpointGeneration;

    public static StudioDevelopmentHost StartForCurrentProcess(
        IStudioDiagnosticHub diagnosticHub,
        StudioInstanceId studioInstanceId,
        StudioSessionId studioSessionId,
        string buildIdentity,
        string configuration,
        long endpointGeneration = 1,
        long providerGeneration = 1,
        IStudioUiObservationSource? uiObservationSource = null)
    {
        ArgumentNullException.ThrowIfNull(diagnosticHub);
        using var process = Process.GetCurrentProcess();
        var processStartTimeUtc = new DateTimeOffset(
            process.StartTime.ToUniversalTime(),
            TimeSpan.Zero);
        return new StudioDevelopmentHost(new DevelopmentObservationSession(
            diagnosticHub,
            studioInstanceId,
            studioSessionId,
            process.Id,
            processStartTimeUtc,
            buildIdentity,
            configuration,
            endpointGeneration,
            providerGeneration,
            uiObservationSource));
    }

    public ValueTask<ObservationResponse<ToolSessionDescriptor>> DescribeAsync(
        ObservationRequest<SessionDescribeParameters> request,
        CancellationToken cancellationToken = default) =>
        DispatchAsync(
            request,
            ObservationMethodId.SessionDescribe,
            _ => ValueTask.FromResult(new ObservationProtocolReadResult<ToolSessionDescriptor>(
                session_.Describe(StateName()),
                Failure: null)),
            truncation: null,
            cancellationToken);

    public ValueTask<ObservationResponse<ObservationCursorWindow<ObservationDiagnosticEvent>>>
        ReadDiagnosticsAsync(
            ObservationRequest<DiagnosticsReadParameters> request,
            CancellationToken cancellationToken = default) =>
        DispatchAsync(
            request,
            ObservationMethodId.DiagnosticsRead,
            _ => ValueTask.FromResult(session_.ReadDiagnostics(request.Parameters)),
            CursorTruncation,
            cancellationToken);

    public ValueTask<ObservationResponse<ObservationCursorWindow<ObservationLogEvent>>>
        ReadLogsAsync(
            ObservationRequest<LogsReadParameters> request,
            CancellationToken cancellationToken = default) =>
        DispatchAsync(
            request,
            ObservationMethodId.LogsRead,
            _ => ValueTask.FromResult(session_.ReadLogs(request.Parameters)),
            CursorTruncation,
            cancellationToken);

    public ValueTask<ObservationResponse<UiWindowListResult>> ListWindowsAsync(
        ObservationRequest<UiListWindowsParameters> request,
        CancellationToken cancellationToken = default) =>
        DispatchAsync(
            request,
            ObservationMethodId.UiListWindows,
            token => session_.ListWindowsAsync(request.Parameters, token),
            truncation: null,
            cancellationToken);

    public ValueTask<ObservationResponse<UiTreeReadResult>> ReadTreeAsync(
        ObservationRequest<UiReadTreeParameters> request,
        CancellationToken cancellationToken = default) =>
        DispatchAsync(
            request,
            ObservationMethodId.UiReadTree,
            token => session_.ReadTreeAsync(request.Parameters, token),
            UiTreeTruncation,
            cancellationToken);

    public ValueTask<StudioDevelopmentHostTeardownReceipt> StopAsync(
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (timeout < TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        Task<StudioDevelopmentHostTeardownReceipt> stopTask;
        lock (stopGate_)
        {
            stopTask_ ??= StopCoreAsync(timeout);
            stopTask = stopTask_;
        }

        return new ValueTask<StudioDevelopmentHostTeardownReceipt>(
            stopTask.WaitAsync(cancellationToken));
    }

    public async ValueTask DisposeAsync()
    {
        var receipt = await StopAsync(DefaultDisposeTimeout).ConfigureAwait(false);
        if (receipt.Status != StudioDevelopmentHostStopStatus.Completed)
        {
            throw new TimeoutException(
                "Studio development host did not stop within its owner deadline.");
        }
    }

    private async ValueTask<ObservationResponse<TValue>> DispatchAsync<TParameters, TValue>(
        ObservationRequest<TParameters> request,
        ObservationMethodId expectedMethod,
        Func<CancellationToken, ValueTask<ObservationProtocolReadResult<TValue>>> read,
        Func<TValue, ObservationTruncation?>? truncation,
        CancellationToken cancellationToken)
        where TParameters : class
        where TValue : class
    {
        ArgumentNullException.ThrowIfNull(request);
        var validationFailure = ValidateRequest(request, expectedMethod);
        if (validationFailure is not null)
        {
            return Failed<TValue>(request.RequestId, validationFailure);
        }

        if (State != StudioDevelopmentHostState.Running)
        {
            return Failed<TValue>(
                request.RequestId,
                HostUnavailable("The Studio development host is stopping or stopped."));
        }

        using var deadlineCancellation = new CancellationTokenSource(
            TimeSpan.FromMilliseconds(request.TimeoutMilliseconds));
        using var dispatchCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadlineCancellation.Token,
            lifetimeCancellation_.Token);
        var gateAcquired = false;
        try
        {
            await dispatchGate_.WaitAsync(dispatchCancellation.Token)
                .ConfigureAwait(false);
            gateAcquired = true;
            dispatchCancellation.Token.ThrowIfCancellationRequested();
            if (State != StudioDevelopmentHostState.Running)
            {
                return Failed<TValue>(
                    request.RequestId,
                    HostUnavailable("The Studio development host stopped before dispatch."));
            }

            var result = await read(dispatchCancellation.Token).ConfigureAwait(false);
            dispatchCancellation.Token.ThrowIfCancellationRequested();
            if (!result.Succeeded)
            {
                return Failed<TValue>(
                    request.RequestId,
                    result.Failure ?? new ObservationFailure(
                        "observation.provider.invalid-result",
                        "provider",
                        "Observation provider returned neither a value nor a typed failure.",
                        Retryable: false));
            }

            var value = result.Value!;
            if (!IsValidProviderValue(value))
            {
                return Failed<TValue>(
                    request.RequestId,
                    new ObservationFailure(
                        "observation.provider.invalid-result",
                        "provider",
                        "Observation provider returned a value outside its typed bounds.",
                        Retryable: false,
                        CapabilityId: expectedMethod.Value));
            }

            var responseTruncation = truncation?.Invoke(value);
            return new ObservationResponse<TValue>(
                ObservationProtocolVersion.Current,
                request.RequestId,
                session_.StudioInstanceId,
                session_.EndpointGeneration,
                responseTruncation is null
                    ? ObservationOutcome.Complete
                    : ObservationOutcome.Partial,
                value,
                Failure: null,
                responseTruncation);
        }
        catch (OperationCanceledException)
        {
            if (cancellationToken.IsCancellationRequested)
            {
                return Cancelled<TValue>(
                    request.RequestId,
                    "observation.request.cancelled",
                    "Observation request was cancelled by its caller.");
            }

            if (lifetimeCancellation_.IsCancellationRequested)
            {
                return Cancelled<TValue>(
                    request.RequestId,
                    "observation.host.stopping",
                    "Observation request was cancelled because the host is stopping.");
            }

            return TimedOut<TValue>(request.RequestId, request.TimeoutMilliseconds);
        }
        catch (Exception error)
        {
            return Failed<TValue>(
                request.RequestId,
                new ObservationFailure(
                    "observation.provider.faulted",
                    "provider",
                    "Observation provider failed while producing a bounded snapshot.",
                    Retryable: false,
                    CapabilityId: expectedMethod.Value,
                    Attributes:
                    [
                        new ObservationSafeAttribute(
                            "exceptionType",
                            error.GetType().FullName ?? error.GetType().Name),
                    ]));
        }
        finally
        {
            if (gateAcquired)
            {
                dispatchGate_.Release();
            }
        }
    }

    private async Task<StudioDevelopmentHostTeardownReceipt> StopCoreAsync(
        TimeSpan timeout)
    {
        var startedAtUtc = DateTimeOffset.UtcNow;
        SetState(StudioDevelopmentHostState.Stopping);
        await lifetimeCancellation_.CancelAsync().ConfigureAwait(false);

        var acquired = await dispatchGate_.WaitAsync(timeout).ConfigureAwait(false);
        if (!acquired)
        {
            _ = CompleteStopAfterDrainAsync();
            return new StudioDevelopmentHostTeardownReceipt(
                StudioDevelopmentHostStopStatus.TimedOut,
                startedAtUtc,
                DateTimeOffset.UtcNow);
        }

        dispatchGate_.Release();
        SetState(StudioDevelopmentHostState.Stopped);
        return new StudioDevelopmentHostTeardownReceipt(
            StudioDevelopmentHostStopStatus.Completed,
            startedAtUtc,
            DateTimeOffset.UtcNow);
    }

    private async Task CompleteStopAfterDrainAsync()
    {
        await dispatchGate_.WaitAsync().ConfigureAwait(false);
        dispatchGate_.Release();
        SetState(StudioDevelopmentHostState.Stopped);
    }

    private ObservationFailure? ValidateRequest<TParameters>(
        ObservationRequest<TParameters> request,
        ObservationMethodId expectedMethod)
        where TParameters : class
    {
        if (request.Protocol.Major != ObservationProtocolVersion.Current.Major
            || request.Protocol.Minor < 0)
        {
            return new ObservationFailure(
                "observation.protocol.unsupported",
                "protocol",
                "Request protocol is incompatible with the running v1 host.",
                Retryable: false);
        }

        if (request.RequestId.Value == Guid.Empty
            || request.StudioInstanceId != session_.StudioInstanceId
            || request.EndpointGeneration != session_.EndpointGeneration
            || request.TimeoutMilliseconds <= 0
            || request.TimeoutMilliseconds > ObservationProtocolLimits.MaxRequestTimeoutMilliseconds
            || request.Parameters is null)
        {
            return new ObservationFailure(
                "observation.request.invalid",
                "protocol",
                "Request identity, generation, timeout, and typed parameters must match the running host.",
                Retryable: false);
        }

        if (!string.Equals(
                request.Method.Value,
                expectedMethod.Value,
                StringComparison.Ordinal))
        {
            return new ObservationFailure(
                "observation.protocol.unsupported",
                "protocol",
                "Request method does not match this typed host operation.",
                Retryable: false);
        }

        if (request.Parameters is UiReadTreeParameters uiParameters
            && !ObservationUiContract.IsValidReadTreeParameters(uiParameters))
        {
            return new ObservationFailure(
                "observation.request.invalid",
                "protocol",
                "UI tree request identity and budgets are outside the typed protocol limits.",
                Retryable: false,
                CapabilityId: expectedMethod.Value);
        }

        return null;
    }

    private ObservationResponse<T> Failed<T>(
        ObservationRequestId requestId,
        ObservationFailure failure)
        where T : class =>
        Terminal<T>(requestId, ObservationOutcome.Failed, failure);

    private ObservationResponse<T> Cancelled<T>(
        ObservationRequestId requestId,
        string code,
        string message)
        where T : class =>
        Terminal<T>(
            requestId,
            ObservationOutcome.Cancelled,
            new ObservationFailure(
                code,
                "operation",
                message,
                Retryable: true));

    private ObservationResponse<T> TimedOut<T>(
        ObservationRequestId requestId,
        int timeoutMilliseconds)
        where T : class =>
        Terminal<T>(
            requestId,
            ObservationOutcome.TimedOut,
            new ObservationFailure(
                "observation.request.timed-out",
                "operation",
                $"Observation request exceeded its {timeoutMilliseconds}-millisecond deadline.",
                Retryable: true));

    private ObservationResponse<T> Terminal<T>(
        ObservationRequestId requestId,
        ObservationOutcome outcome,
        ObservationFailure failure)
        where T : class =>
        new(
            ObservationProtocolVersion.Current,
            requestId,
            session_.StudioInstanceId,
            session_.EndpointGeneration,
            outcome,
            Value: null,
            failure);

    private static ObservationFailure HostUnavailable(string message) =>
        new(
            "observation.host.unavailable",
            "host",
            message,
            Retryable: false);

    private static ObservationTruncation? CursorTruncation<T>(
        ObservationCursorWindow<T> window)
        where T : class =>
        window.CursorExpired || window.Truncated
            ? new ObservationTruncation(
                IsTruncated: true,
                Reason: window.CursorExpired ? "cursor-expired" : "page-truncated",
                DroppedCount: window.TotalDropped)
            : null;

    private static ObservationTruncation? UiTreeTruncation(UiTreeReadResult tree) =>
        tree.IsTruncated
            ? new ObservationTruncation(
                IsTruncated: true,
                Reason: tree.TruncationReason)
            : null;

    private static bool IsValidProviderValue<T>(T value)
        where T : class =>
        value switch
        {
            UiWindowListResult windows => ObservationUiContract.IsValidWindowListResult(windows),
            UiTreeReadResult tree => ObservationUiContract.IsValidTreeReadResult(tree),
            _ => true,
        };

    private string StateName() => State switch
    {
        StudioDevelopmentHostState.Running => "running",
        StudioDevelopmentHostState.Stopping => "stopping",
        StudioDevelopmentHostState.Stopped => "stopped",
        _ => throw new InvalidOperationException("Unknown development host state."),
    };

    private void SetState(StudioDevelopmentHostState state)
    {
        lock (stateGate_)
        {
            state_ = state;
        }
    }
}

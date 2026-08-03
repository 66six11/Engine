using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Transport;

public enum StudioDevelopmentPipeServerState
{
    Running,
    Stopping,
    Stopped,
    Faulted,
}

public enum StudioDevelopmentPipeStopStatus
{
    Completed,
    TimedOut,
    Faulted,
}

public sealed record StudioDevelopmentPipeTeardownReceipt(
    StudioDevelopmentPipeStopStatus Status,
    DateTimeOffset StartedAtUtc,
    DateTimeOffset CompletedAtUtc,
    string? FailureCode = null);

public sealed class StudioDevelopmentPipeServer : IAsyncDisposable
{
    public const int MaxClients = 4;

    private const int AttachTokenBytes = 32;
    private const int PipeBufferBytes = 4096;
    private const int MaxPipeNameLength = 128;
    private static readonly TimeSpan DefaultDisposeTimeout = TimeSpan.FromSeconds(5);

    private readonly StudioDevelopmentHost host_;
    private readonly string pipeName_;
    private readonly byte[] attachToken_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly object stateGate_ = new();
    private readonly object stopGate_ = new();
    private Task[] workers_;
    private Task<StudioDevelopmentPipeTeardownReceipt>? stopTask_;
    private StudioDevelopmentPipeServerState state_ = StudioDevelopmentPipeServerState.Running;

    private StudioDevelopmentPipeServer(
        StudioDevelopmentHost host,
        string pipeName,
        byte[] attachToken)
    {
        host_ = host;
        pipeName_ = pipeName;
        attachToken_ = attachToken;
        workers_ = [];
    }

    private void StartWorkers()
    {
        workers_ = Enumerable
            .Range(0, MaxClients)
            .Select(_ => AcceptLoopAsync(lifetimeCancellation_.Token))
            .ToArray();
    }

    public string PipeName => pipeName_;

    public StudioDevelopmentPipeServerState State
    {
        get
        {
            lock (stateGate_)
            {
                return state_;
            }
        }
    }

    public static async ValueTask<StudioDevelopmentPipeServer> StartAsync(
        StudioDevelopmentHost host,
        string pipeName,
        string attachToken,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(host);
        cancellationToken.ThrowIfCancellationRequested();
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException(
                "Studio development Named Pipe v1 is currently Windows-only.");
        }

        ValidatePipeName(pipeName);
        var tokenBytes = DecodeAttachToken(attachToken, nameof(attachToken));
        var server = new StudioDevelopmentPipeServer(host, pipeName, tokenBytes);
        try
        {
            server.StartWorkers();
        }
        catch
        {
            CryptographicOperations.ZeroMemory(tokenBytes);
            throw;
        }

        var startupFault = server.workers_
            .FirstOrDefault(worker => worker.IsFaulted)
            ?.Exception;
        if (startupFault is not null)
        {
            await server.AbortStartupAsync().ConfigureAwait(false);
            throw new InvalidOperationException(
                "Studio development pipe failed to start its fixed accept workers.",
                startupFault);
        }

        if (cancellationToken.IsCancellationRequested)
        {
            await server.AbortStartupAsync().ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
        }

        return server;
    }

    private async Task AbortStartupAsync()
    {
        SetState(StudioDevelopmentPipeServerState.Stopping);
        await lifetimeCancellation_.CancelAsync().ConfigureAwait(false);
        try
        {
            await Task.WhenAll(workers_).ConfigureAwait(false);
        }
        catch (Exception)
        {
            // StartAsync preserves the startup fault after every worker has
            // released its pipe instance.
        }

        SetState(StudioDevelopmentPipeServerState.Faulted);
        ZeroToken();
        lifetimeCancellation_.Dispose();
    }

    public static string CreateAttachToken() =>
        Convert.ToBase64String(RandomNumberGenerator.GetBytes(AttachTokenBytes));

    public ValueTask<StudioDevelopmentPipeTeardownReceipt> StopAsync(
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (timeout < TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        Task<StudioDevelopmentPipeTeardownReceipt> stopTask;
        lock (stopGate_)
        {
            stopTask_ ??= StopCoreAsync(timeout);
            stopTask = stopTask_;
        }

        return new ValueTask<StudioDevelopmentPipeTeardownReceipt>(
            stopTask.WaitAsync(cancellationToken));
    }

    public async ValueTask DisposeAsync()
    {
        var receipt = await StopAsync(DefaultDisposeTimeout).ConfigureAwait(false);
        if (receipt.Status == StudioDevelopmentPipeStopStatus.TimedOut)
        {
            throw new TimeoutException(
                "Studio development pipe did not stop within its owner deadline.");
        }

        if (receipt.Status == StudioDevelopmentPipeStopStatus.Faulted)
        {
            throw new InvalidOperationException(
                "Studio development pipe accept loop faulted during teardown.");
        }
    }

    private async Task AcceptLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            using var pipe = new NamedPipeServerStream(
                pipeName_,
                PipeDirection.InOut,
                MaxClients,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous
                | PipeOptions.CurrentUserOnly
                | PipeOptions.WriteThrough,
                PipeBufferBytes,
                PipeBufferBytes);
            try
            {
                await pipe.WaitForConnectionAsync(cancellationToken)
                    .ConfigureAwait(false);
                await HandleConnectionAsync(pipe, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception error) when (IsConnectionFailure(error))
            {
                // A malformed, truncated, or disconnected client only ends its
                // own fixed worker iteration. No request data enters logs.
            }
        }
    }

    private async Task HandleConnectionAsync(
        NamedPipeServerStream pipe,
        CancellationToken cancellationToken)
    {
        var handshakeFrame = await PipeFrameProtocol.ReadAsync(
                pipe,
                ObservationProtocolLimits.MaxRequestBytes,
                cancellationToken)
            .ConfigureAwait(false);
        if (handshakeFrame is null)
        {
            return;
        }

        var parsedHandshake = ObservationProtocolJson.ReadHandshakeRequest(handshakeFrame);
        if (!parsedHandshake.Succeeded)
        {
            return;
        }

        var handshake = parsedHandshake.Value!;
        if (handshake.StudioInstanceId != host_.StudioInstanceId
            || handshake.EndpointGeneration != host_.EndpointGeneration
            || !TokenMatches(handshake.AttachToken))
        {
            await WriteResponseAsync(
                    pipe,
                    new ObservationResponse<ToolSessionDescriptor>(
                        ObservationProtocolVersion.Current,
                        handshake.RequestId,
                        host_.StudioInstanceId,
                        host_.EndpointGeneration,
                        ObservationOutcome.Failed,
                        Value: null,
                        new ObservationFailure(
                            "observation.handshake.denied",
                            "security",
                            "Studio development pipe handshake was denied.",
                            Retryable: false)),
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var descriptor = await host_.DescribeAsync(
                new ObservationRequest<SessionDescribeParameters>(
                    handshake.Protocol,
                    handshake.RequestId,
                    handshake.StudioInstanceId,
                    handshake.EndpointGeneration,
                    ObservationMethodId.SessionDescribe,
                    ObservationProtocolLimits.MaxWaitMilliseconds,
                    new SessionDescribeParameters()),
                cancellationToken)
            .ConfigureAwait(false);
        await WriteResponseAsync(pipe, descriptor, cancellationToken)
            .ConfigureAwait(false);
        if (descriptor.Outcome != ObservationOutcome.Complete)
        {
            return;
        }

        while (!cancellationToken.IsCancellationRequested)
        {
            var requestFrame = await PipeFrameProtocol.ReadAsync(
                    pipe,
                    ObservationProtocolLimits.MaxRequestBytes,
                    cancellationToken)
                .ConfigureAwait(false);
            if (requestFrame is null
                || !await TryDispatchAsync(pipe, requestFrame, cancellationToken)
                    .ConfigureAwait(false))
            {
                return;
            }
        }
    }

    private async ValueTask<bool> TryDispatchAsync(
        NamedPipeServerStream pipe,
        byte[] requestFrame,
        CancellationToken cancellationToken)
    {
        var describe = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            requestFrame,
            ObservationMethodId.SessionDescribe);
        if (describe.Succeeded)
        {
            await WriteResponseAsync(
                    pipe,
                    await host_.DescribeAsync(describe.Value!, cancellationToken)
                        .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        var diagnostics = ObservationProtocolJson.ReadRequest<DiagnosticsReadParameters>(
            requestFrame,
            ObservationMethodId.DiagnosticsRead);
        if (diagnostics.Succeeded)
        {
            await WriteResponseAsync(
                    pipe,
                    await host_.ReadDiagnosticsAsync(diagnostics.Value!, cancellationToken)
                        .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        var logs = ObservationProtocolJson.ReadRequest<LogsReadParameters>(
            requestFrame,
            ObservationMethodId.LogsRead);
        if (logs.Succeeded)
        {
            await WriteResponseAsync(
                    pipe,
                    await host_.ReadLogsAsync(logs.Value!, cancellationToken)
                        .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        var windows = ObservationProtocolJson.ReadRequest<UiListWindowsParameters>(
            requestFrame,
            ObservationMethodId.UiListWindows);
        if (windows.Succeeded)
        {
            await WriteResponseAsync(
                    pipe,
                    await host_.ListWindowsAsync(windows.Value!, cancellationToken)
                        .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        var tree = ObservationProtocolJson.ReadRequest<UiReadTreeParameters>(
            requestFrame,
            ObservationMethodId.UiReadTree);
        if (tree.Succeeded)
        {
            await WriteResponseAsync(
                    pipe,
                    await host_.ReadTreeAsync(tree.Value!, cancellationToken)
                        .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        return false;
    }

    private static ValueTask WriteResponseAsync<T>(
        Stream pipe,
        ObservationResponse<T> response,
        CancellationToken cancellationToken)
        where T : class =>
        PipeFrameProtocol.WriteAsync(
            pipe,
            ObservationProtocolJson.WriteResponse(response),
            ObservationProtocolLimits.MaxResponseBytes,
            cancellationToken);

    private async Task<StudioDevelopmentPipeTeardownReceipt> StopCoreAsync(
        TimeSpan timeout)
    {
        var startedAtUtc = DateTimeOffset.UtcNow;
        SetState(StudioDevelopmentPipeServerState.Stopping);
        await lifetimeCancellation_.CancelAsync().ConfigureAwait(false);
        var allWorkers = Task.WhenAll(workers_);
        try
        {
            await allWorkers.WaitAsync(timeout).ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            _ = CompleteAfterWorkersAsync(allWorkers);
            return new StudioDevelopmentPipeTeardownReceipt(
                StudioDevelopmentPipeStopStatus.TimedOut,
                startedAtUtc,
                DateTimeOffset.UtcNow);
        }
        catch (Exception)
        {
            SetState(StudioDevelopmentPipeServerState.Faulted);
            ZeroToken();
            return new StudioDevelopmentPipeTeardownReceipt(
                StudioDevelopmentPipeStopStatus.Faulted,
                startedAtUtc,
                DateTimeOffset.UtcNow,
                "observation.pipe.worker-faulted");
        }

        SetState(StudioDevelopmentPipeServerState.Stopped);
        ZeroToken();
        return new StudioDevelopmentPipeTeardownReceipt(
            StudioDevelopmentPipeStopStatus.Completed,
            startedAtUtc,
            DateTimeOffset.UtcNow);
    }

    private async Task CompleteAfterWorkersAsync(Task allWorkers)
    {
        try
        {
            await allWorkers.ConfigureAwait(false);
            SetState(StudioDevelopmentPipeServerState.Stopped);
        }
        catch (Exception)
        {
            SetState(StudioDevelopmentPipeServerState.Faulted);
        }
        finally
        {
            ZeroToken();
        }
    }

    private bool TokenMatches(string presented)
    {
        byte[] presentedBytes;
        try
        {
            presentedBytes = Convert.FromBase64String(presented);
        }
        catch (FormatException)
        {
            return false;
        }

        try
        {
            return string.Equals(
                    Convert.ToBase64String(presentedBytes),
                    presented,
                    StringComparison.Ordinal)
                && presentedBytes.Length == attachToken_.Length
                && CryptographicOperations.FixedTimeEquals(
                    presentedBytes,
                    attachToken_);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(presentedBytes);
        }
    }

    private void ZeroToken() => CryptographicOperations.ZeroMemory(attachToken_);

    private static bool IsConnectionFailure(Exception error) =>
        error is IOException
            or InvalidDataException
            or ArgumentException
            or CryptographicException;

    private static byte[] DecodeAttachToken(string value, string parameterName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameterName);
        if (value.Length > ObservationProtocolLimits.MaxAttachTokenCharacters)
        {
            throw new ArgumentOutOfRangeException(parameterName);
        }

        byte[] bytes;
        try
        {
            bytes = Convert.FromBase64String(value);
        }
        catch (FormatException error)
        {
            throw new ArgumentException(
                "Attach token must be canonical base64.",
                parameterName,
                error);
        }

        if (bytes.Length != AttachTokenBytes)
        {
            CryptographicOperations.ZeroMemory(bytes);
            throw new ArgumentException(
                $"Attach token must contain exactly {AttachTokenBytes} random bytes.",
                parameterName);
        }

        if (!string.Equals(
                Convert.ToBase64String(bytes),
                value,
                StringComparison.Ordinal))
        {
            CryptographicOperations.ZeroMemory(bytes);
            throw new ArgumentException(
                "Attach token must use canonical base64 encoding.",
                parameterName);
        }

        return bytes;
    }

    private static void ValidatePipeName(string pipeName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(pipeName);
        if (pipeName.Length > MaxPipeNameLength
            || pipeName.Any(character => !char.IsAsciiLetterOrDigit(character)
                && character is not '.' and not '-' and not '_'))
        {
            throw new ArgumentException(
                "Pipe name must be at most 128 ASCII letters, digits, dots, dashes, or underscores.",
                nameof(pipeName));
        }
    }

    private void SetState(StudioDevelopmentPipeServerState state)
    {
        lock (stateGate_)
        {
            state_ = state;
        }
    }
}

using System;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.Observe.Client;

internal sealed record StudioObservationConnectResult(
    StudioObservationConnection? Connection,
    ObservationResponse<ToolSessionDescriptor>? Response,
    ObservationFailure? Failure)
{
    internal bool Succeeded => Connection is not null
        && Response?.Outcome == ObservationOutcome.Complete
        && Failure is null;
}

internal sealed record StudioObservationOperationResult<TValue>(
    ObservationResponse<TValue>? Response,
    ObservationFailure? Failure)
    where TValue : class
{
    internal bool Succeeded => Response?.Outcome is
        ObservationOutcome.Complete or ObservationOutcome.Partial
        && Response.Value is not null
        && Failure is null;
}

internal sealed class StudioObservationConnection : IAsyncDisposable
{
    private readonly NamedPipeClientStream pipe_;

    internal StudioObservationConnection(
        NamedPipeClientStream pipe,
        ObservationResponse<ToolSessionDescriptor> response)
    {
        pipe_ = pipe;
        DescribeResponse = response;
    }

    internal ObservationResponse<ToolSessionDescriptor> DescribeResponse { get; }

    internal async ValueTask<StudioObservationOperationResult<
        ObservationCursorWindow<ObservationDiagnosticEvent>>> ReadDiagnosticsAsync(
        DiagnosticsReadParameters parameters,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var result = await SendAsync<
                DiagnosticsReadParameters,
                ObservationCursorWindow<ObservationDiagnosticEvent>>(
                ObservationMethodId.DiagnosticsRead,
                parameters,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return ValidateCursor(
            result,
            parameters.AfterSequence,
            parameters.MaxCount,
            static item => item.Sequence);
    }

    internal async ValueTask<StudioObservationOperationResult<
        ObservationCursorWindow<ObservationLogEvent>>> ReadLogsAsync(
        LogsReadParameters parameters,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var result = await SendAsync<
                LogsReadParameters,
                ObservationCursorWindow<ObservationLogEvent>>(
                ObservationMethodId.LogsRead,
                parameters,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return ValidateCursor(
            result,
            parameters.AfterSequence,
            parameters.MaxCount,
            static item => item.Sequence);
    }

    internal ValueTask<StudioObservationOperationResult<UiWindowListResult>> ListWindowsAsync(
        TimeSpan timeout,
        CancellationToken cancellationToken) =>
        HasCapability("ui.listWindows")
            ? SendAsync<UiListWindowsParameters, UiWindowListResult>(
                ObservationMethodId.UiListWindows,
                new UiListWindowsParameters(),
                timeout,
                cancellationToken)
            : ValueTask.FromResult(CapabilityUnavailable<UiWindowListResult>(
                "ui.listWindows"));

    internal async ValueTask<StudioObservationOperationResult<UiTreeReadResult>> ReadTreeAsync(
        UiReadTreeParameters parameters,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (!HasCapability("ui.readTree"))
        {
            return CapabilityUnavailable<UiTreeReadResult>("ui.readTree");
        }

        if (!ObservationUiContract.IsValidReadTreeParameters(parameters))
        {
            return Failed<UiTreeReadResult>(
                "observation.client.invalid-request",
                "protocol",
                "UI tree request identity and budgets are outside the typed protocol limits.");
        }

        var result = await SendAsync<UiReadTreeParameters, UiTreeReadResult>(
                ObservationMethodId.UiReadTree,
                parameters,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        if (!result.Succeeded)
        {
            return result;
        }

        var tree = result.Response!.Value!;
        if (!string.Equals(tree.WindowId, parameters.WindowId, StringComparison.Ordinal)
            || tree.Nodes.Length > parameters.MaxNodes
            || tree.Nodes.Any(node => node.Depth > parameters.MaxDepth))
        {
            return Failed<UiTreeReadResult>(
                "observation.client.invalid-ui-tree",
                "protocol",
                "Studio UI tree response exceeds its requested identity or budgets.");
        }

        return result;
    }

    public ValueTask DisposeAsync() => pipe_.DisposeAsync();

    private async ValueTask<StudioObservationOperationResult<TValue>> SendAsync<
        TParameters,
        TValue>(
        ObservationMethodId method,
        TParameters parameters,
        TimeSpan timeout,
        CancellationToken cancellationToken)
        where TParameters : class
        where TValue : class
    {
        ArgumentNullException.ThrowIfNull(parameters);
        if (timeout <= TimeSpan.Zero
            || timeout > TimeSpan.FromMilliseconds(
                ObservationProtocolLimits.MaxRequestTimeoutMilliseconds))
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        var requestId = new ObservationRequestId(Guid.NewGuid());
        var descriptor = DescribeResponse.Value!;
        var timeoutMilliseconds = Math.Clamp(
            (int)Math.Ceiling(timeout.TotalMilliseconds),
            1,
            ObservationProtocolLimits.MaxRequestTimeoutMilliseconds);
        var request = new ObservationRequest<TParameters>(
            ObservationProtocolVersion.Current,
            requestId,
            descriptor.StudioInstanceId,
            descriptor.EndpointGeneration,
            method,
            timeoutMilliseconds,
            parameters);
        using var deadline = new CancellationTokenSource(timeout);
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadline.Token);
        try
        {
            await PipeFrameClientProtocol.WriteAsync(
                    pipe_,
                    ObservationProtocolJson.WriteRequest(request),
                    ObservationProtocolLimits.MaxRequestBytes,
                    operation.Token)
                .ConfigureAwait(false);
            var frame = await PipeFrameClientProtocol.ReadAsync(
                    pipe_,
                    ObservationProtocolLimits.MaxResponseBytes,
                    operation.Token)
                .ConfigureAwait(false);
            if (frame is null)
            {
                return Failed<TValue>(
                    "observation.client.unavailable",
                    "unavailable",
                    "Studio observation endpoint closed before returning a response.");
            }

            var parsed = ObservationProtocolJson.ReadResponse<TValue>(frame);
            if (!parsed.Succeeded)
            {
                return new StudioObservationOperationResult<TValue>(
                    Response: null,
                    parsed.Failure);
            }

            var response = parsed.Value!;
            if (response.RequestId != requestId
                || response.StudioInstanceId != descriptor.StudioInstanceId
                || response.EndpointGeneration != descriptor.EndpointGeneration)
            {
                return Failed<TValue>(
                    "observation.client.identity-mismatch",
                    "protocol",
                    "Studio observation response identity does not match its request.");
            }

            if (response.Outcome is ObservationOutcome.Complete
                or ObservationOutcome.Partial)
            {
                return response.Value is null
                    ? Failed<TValue>(
                        "observation.client.invalid-response",
                        "protocol",
                        "Studio observation response did not contain its typed value.")
                    : new StudioObservationOperationResult<TValue>(
                        response,
                        Failure: null);
            }

            return new StudioObservationOperationResult<TValue>(
                response,
                response.Failure ?? new ObservationFailure(
                    "observation.client.invalid-response",
                    "protocol",
                    "Studio observation response did not contain a typed failure.",
                    Retryable: false));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Failed<TValue>(
                "observation.client.cancelled",
                "operation",
                "Studio observation request was cancelled.",
                retryable: true);
        }
        catch (OperationCanceledException)
        {
            return Failed<TValue>(
                "observation.client.timed-out",
                "operation",
                "Studio observation request exceeded its client deadline.",
                retryable: true);
        }
        catch (InvalidDataException)
        {
            return Failed<TValue>(
                "observation.client.protocol-invalid",
                "protocol",
                "Studio observation endpoint returned an invalid bounded frame.");
        }
        catch (IOException)
        {
            return Failed<TValue>(
                "observation.client.unavailable",
                "unavailable",
                "Studio observation endpoint is unavailable.");
        }
    }

    private static StudioObservationOperationResult<TValue> Failed<TValue>(
        string code,
        string category,
        string message,
        bool retryable = false)
        where TValue : class =>
        new(
            Response: null,
            new ObservationFailure(
                code,
                category,
                message,
                retryable));

    private StudioObservationOperationResult<TValue> CapabilityUnavailable<TValue>(
        string capabilityId)
        where TValue : class =>
        new(
            Response: null,
            new ObservationFailure(
                "observation.capability.unavailable",
                "unavailable",
                "Attached Studio session does not advertise the requested UI capability.",
                Retryable: false,
                CapabilityId: capabilityId));

    private bool HasCapability(string capabilityId) =>
        DescribeResponse.Value!.Capabilities.Any(capability =>
            string.Equals(capability.CapabilityId, capabilityId, StringComparison.Ordinal)
            && string.Equals(capability.Availability, "available", StringComparison.Ordinal));

    internal static StudioObservationOperationResult<ObservationCursorWindow<T>>
        ValidateCursor<T>(
            StudioObservationOperationResult<ObservationCursorWindow<T>> result,
            long requestedAfterSequence,
            int requestedMaxCount,
            Func<T, long> sequence)
        where T : class
    {
        if (!result.Succeeded)
        {
            return result;
        }

        var response = result.Response!;
        var window = response.Value!;
        var oldestValid = window.OldestAvailableSequence >= 1;
        var expectedCursorExpired = oldestValid
            && requestedAfterSequence < window.OldestAvailableSequence - 1;
        var semanticPartial = window.CursorExpired || window.Truncated;
        var previousSequence = requestedAfterSequence;
        var sequencesValid = !window.Items.IsDefault;
        if (sequencesValid)
        {
            foreach (var item in window.Items)
            {
                var currentSequence = sequence(item);
                if (currentSequence < window.OldestAvailableSequence
                    || currentSequence <= previousSequence)
                {
                    sequencesValid = false;
                    break;
                }

                previousSequence = currentSequence;
            }
        }

        if (!oldestValid
            || window.NextCursor < 0
            || window.NextCursor < requestedAfterSequence
            || window.TotalDropped < 0
            || window.TotalDropped < window.OldestAvailableSequence - 1
            || window.CursorExpired != expectedCursorExpired
            || window.Items.IsDefault
            || window.Items.Length > requestedMaxCount
            || window.Items.Length > ObservationProtocolLimits.MaxPageSize
            || !sequencesValid
            || (window.Items.Length != 0 && window.NextCursor < previousSequence)
            || semanticPartial != (response.Outcome == ObservationOutcome.Partial))
        {
            return Failed<ObservationCursorWindow<T>>(
                "observation.client.invalid-cursor",
                "protocol",
                "Studio observation cursor response violates its typed bounds or ordering.");
        }

        return result;
    }
}

internal static class StudioObservationClient
{
    internal static async ValueTask<StudioObservationConnectResult> ConnectAsync(
        DevelopmentSessionManifest manifest,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        if (!OperatingSystem.IsWindows())
        {
            return Failed(
                "observation.client.unsupported-platform",
                "unavailable",
                "Studio observation Named Pipe client is currently Windows-only.");
        }

        if (timeout <= TimeSpan.Zero
            || timeout > TimeSpan.FromMilliseconds(
                ObservationProtocolLimits.MaxRequestTimeoutMilliseconds))
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        var pipe = new NamedPipeClientStream(
            ".",
            manifest.PipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        using var deadline = new CancellationTokenSource(timeout);
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadline.Token);
        try
        {
            await pipe.ConnectAsync(timeout, operation.Token).ConfigureAwait(false);
            var request = new ObservationHandshakeRequest(
                ObservationProtocolVersion.Current,
                new ObservationRequestId(Guid.NewGuid()),
                manifest.StudioInstanceId,
                manifest.EndpointGeneration,
                manifest.AttachToken);
            await PipeFrameClientProtocol.WriteAsync(
                    pipe,
                    ObservationProtocolJson.WriteHandshakeRequest(request),
                    ObservationProtocolLimits.MaxRequestBytes,
                    operation.Token)
                .ConfigureAwait(false);
            var frame = await PipeFrameClientProtocol.ReadAsync(
                    pipe,
                    ObservationProtocolLimits.MaxResponseBytes,
                    operation.Token)
                .ConfigureAwait(false);
            if (frame is null)
            {
                await pipe.DisposeAsync().ConfigureAwait(false);
                return Failed(
                    "observation.client.unavailable",
                    "unavailable",
                    "Studio observation endpoint closed before completing its handshake.");
            }

            var parsed = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(frame);
            if (!parsed.Succeeded)
            {
                await pipe.DisposeAsync().ConfigureAwait(false);
                return new StudioObservationConnectResult(
                    Connection: null,
                    Response: null,
                    parsed.Failure);
            }

            var response = parsed.Value!;
            if (response.Outcome != ObservationOutcome.Complete
                || response.Value is null)
            {
                await pipe.DisposeAsync().ConfigureAwait(false);
                return new StudioObservationConnectResult(
                    Connection: null,
                    response,
                    response.Failure ?? Failure(
                        "observation.client.invalid-response",
                        "protocol",
                        "Studio observation handshake returned no descriptor or typed failure."));
            }

            var identityFailure = ValidateDescriptor(manifest, response.Value);
            if (identityFailure is not null)
            {
                await pipe.DisposeAsync().ConfigureAwait(false);
                return new StudioObservationConnectResult(
                    Connection: null,
                    response,
                    identityFailure);
            }

            return new StudioObservationConnectResult(
                new StudioObservationConnection(pipe, response),
                response,
                Failure: null);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.cancelled",
                "operation",
                "Studio observation request was cancelled.");
        }
        catch (OperationCanceledException)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.timed-out",
                "operation",
                "Studio observation endpoint did not respond within the client deadline.");
        }
        catch (TimeoutException)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.timed-out",
                "operation",
                "Studio observation endpoint did not connect within the client deadline.");
        }
        catch (UnauthorizedAccessException)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.denied",
                "security",
                "Current-user Studio observation endpoint access was denied.");
        }
        catch (InvalidDataException)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.protocol-invalid",
                "protocol",
                "Studio observation endpoint returned an invalid bounded frame.");
        }
        catch (IOException)
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            return Failed(
                "observation.client.unavailable",
                "unavailable",
                "Studio observation endpoint is unavailable.");
        }
    }

    private static ObservationFailure? ValidateDescriptor(
        DevelopmentSessionManifest manifest,
        ToolSessionDescriptor descriptor)
    {
        if (descriptor.Capabilities.IsDefault
            || descriptor.Capabilities.Length > 64
            || descriptor.Protocol.Major != manifest.Protocol.Major
            || descriptor.Protocol.Minor < 0
            || descriptor.StudioInstanceId != manifest.StudioInstanceId
            || descriptor.StudioSessionId != manifest.StudioSessionId
            || descriptor.ProcessId != manifest.ProcessId
            || descriptor.ProcessStartTimeUtc != manifest.ProcessStartTimeUtc
            || descriptor.EndpointGeneration != manifest.EndpointGeneration
            || !string.Equals(
                descriptor.BuildIdentity,
                manifest.BuildIdentity,
                StringComparison.Ordinal)
            || !string.Equals(
                descriptor.Configuration,
                manifest.Configuration,
                StringComparison.Ordinal)
            || !string.Equals(
                CapabilityDigest(descriptor),
                manifest.CapabilityDigest,
                StringComparison.Ordinal))
        {
            return Failure(
                "observation.client.stale-manifest",
                "stale",
                "Discovery manifest does not match the attached Studio session.");
        }

        return null;
    }

    private static string CapabilityDigest(ToolSessionDescriptor descriptor)
    {
        var canonical = string.Join(
            '\n',
            descriptor.Capabilities
                .OrderBy(capability => capability.CapabilityId, StringComparer.Ordinal)
                .Select(capability => string.Join(
                    '|',
                    capability.CapabilityId,
                    capability.SchemaVersion,
                    capability.Access,
                    capability.Availability,
                    capability.ProviderGeneration,
                    capability.Limits.MaxPageSize,
                    capability.Limits.MaxResponseBytes,
                    capability.Limits.MaxWaitMilliseconds)));
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
    }

    private static StudioObservationConnectResult Failed(
        string code,
        string category,
        string message) =>
        new(
            Connection: null,
            Response: null,
            Failure(code, category, message));

    private static ObservationFailure Failure(
        string code,
        string category,
        string message) =>
        new(code, category, message, Retryable: false);
}

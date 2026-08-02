using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.Client;
using Asharia.Studio.Observe.Discovery;

namespace Asharia.Studio.Observe.CommandLine;

internal enum StudioObserveExitCode
{
    Success = 0,
    Usage = 2,
    Partial = 3,
    Protocol = 10,
    Authorization = 11,
    Stale = 12,
    Unavailable = 13,
    Failed = 14,
    TimedOut = 15,
    Cancelled = 130,
}

internal enum StudioObserveOutputFormat
{
    Text,
    Json,
}

internal enum StudioObserveVerb
{
    List,
    Describe,
    Diagnostics,
    Logs,
    UiListWindows,
    UiReadTree,
}

internal sealed record StudioObserveInvocation(
    StudioObserveVerb Verb,
    StudioInstanceId? StudioInstanceId,
    StudioObserveOutputFormat Format,
    int TimeoutMilliseconds,
    long AfterSequence,
    int MaxCount,
    string? DiagnosticChannel,
    string? WindowId,
    int MaxDepth);

internal sealed record SafeStudioSessionSummary(
    string StudioInstanceId,
    string StudioSessionId,
    int ProcessId,
    DateTimeOffset ProcessStartTimeUtc,
    long EndpointGeneration,
    string BuildIdentity,
    string Configuration,
    string CapabilityDigest,
    DateTimeOffset CreatedAtUtc,
    DateTimeOffset HeartbeatUtc);

internal sealed record SafeDiscoveryIssue(
    string ManifestName,
    string Code,
    string Category,
    string Message);

internal sealed record SafeSessionListOutput(
    ObservationProtocolVersion Protocol,
    ImmutableArray<SafeStudioSessionSummary> Sessions,
    ImmutableArray<SafeDiscoveryIssue> Issues);

internal static class StudioObserveCommand
{
    private const int DefaultTimeoutMilliseconds = 2_000;
    private const int MaxArguments = 16;
    private static readonly JsonSerializerOptions OutputJson = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
    };

    internal static async Task<int> RunAsync(
        string[] arguments,
        TextWriter output,
        TextWriter error,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(error);
        var parsed = Parse(arguments);
        if (parsed.Invocation is null)
        {
            await error.WriteLineAsync(parsed.Error).ConfigureAwait(false);
            await error.WriteLineAsync(Usage()).ConfigureAwait(false);
            return (int)StudioObserveExitCode.Usage;
        }

        if (!OperatingSystem.IsWindows())
        {
            await error.WriteLineAsync(
                "observation.client.unsupported-platform: Studio observation CLI is currently Windows-only.")
                .ConfigureAwait(false);
            return (int)StudioObserveExitCode.Unavailable;
        }

        using var deadline = new CancellationTokenSource(
            TimeSpan.FromMilliseconds(parsed.Invocation.TimeoutMilliseconds));
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadline.Token);
        try
        {
            var discovery = new StudioSessionDiscovery();
            return parsed.Invocation.Verb switch
            {
                StudioObserveVerb.List => await ListAsync(
                    discovery,
                    parsed.Invocation.Format,
                    output,
                    operation.Token),
                StudioObserveVerb.Describe => await DescribeAsync(
                    discovery,
                    parsed.Invocation,
                    output,
                    error,
                    operation.Token,
                    cancellationToken),
                StudioObserveVerb.Diagnostics => await ReadDiagnosticsAsync(
                    discovery,
                    parsed.Invocation,
                    output,
                    error,
                    operation.Token,
                    cancellationToken),
                StudioObserveVerb.Logs => await ReadLogsAsync(
                    discovery,
                    parsed.Invocation,
                    output,
                    error,
                    operation.Token,
                    cancellationToken),
                StudioObserveVerb.UiListWindows => await ListUiWindowsAsync(
                    discovery,
                    parsed.Invocation,
                    output,
                    error,
                    operation.Token,
                    cancellationToken),
                StudioObserveVerb.UiReadTree => await ReadUiTreeAsync(
                    discovery,
                    parsed.Invocation,
                    output,
                    error,
                    operation.Token,
                    cancellationToken),
                _ => throw new InvalidOperationException("Unknown Studio observe verb."),
            };
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            await error.WriteLineAsync(
                "observation.client.cancelled: Studio observation command was cancelled.")
                .ConfigureAwait(false);
            return (int)StudioObserveExitCode.Cancelled;
        }
        catch (OperationCanceledException)
        {
            await error.WriteLineAsync(
                "observation.client.timed-out: Studio observation command exceeded its deadline.")
                .ConfigureAwait(false);
            return (int)StudioObserveExitCode.TimedOut;
        }
        catch (Exception exception) when (exception is IOException
            or UnauthorizedAccessException)
        {
            await error.WriteLineAsync(
                "observation.discovery.unavailable: Studio discovery is unavailable.")
                .ConfigureAwait(false);
            return (int)StudioObserveExitCode.Unavailable;
        }
    }

    internal static (StudioObserveInvocation? Invocation, string? Error) Parse(
        string[] arguments)
    {
        if (arguments.Length == 0)
        {
            return (null, "A command is required.");
        }

        if (arguments.Length > MaxArguments)
        {
            return (null, $"At most {MaxArguments} arguments are allowed.");
        }

        var verb = arguments[0] switch
        {
            "list" => StudioObserveVerb.List,
            "describe" => StudioObserveVerb.Describe,
            "diagnostics" => StudioObserveVerb.Diagnostics,
            "logs" => StudioObserveVerb.Logs,
            "ui-list-windows" => StudioObserveVerb.UiListWindows,
            "ui-read-tree" => StudioObserveVerb.UiReadTree,
            _ => (StudioObserveVerb?)null,
        };
        if (verb is null)
        {
            return (null, $"Unknown command '{arguments[0]}'.");
        }

        StudioInstanceId? instanceId = null;
        var format = StudioObserveOutputFormat.Text;
        var timeoutMilliseconds = DefaultTimeoutMilliseconds;
        long afterSequence = 0;
        var maxCount = 100;
        string? diagnosticChannel = null;
        string? windowId = null;
        var maxDepth = ObservationProtocolLimits.MaxUiDepth;
        var hasFormat = false;
        var hasTimeout = false;
        var hasAfter = false;
        var hasMax = false;
        var hasChannel = false;
        var hasWindow = false;
        var hasMaxDepth = false;
        for (var index = 1; index < arguments.Length; index++)
        {
            var option = arguments[index];
            if (index + 1 >= arguments.Length)
            {
                return (null, $"Option '{option}' requires a value.");
            }

            var value = arguments[++index];
            switch (option)
            {
                case "--instance" when instanceId is null:
                    if (!Guid.TryParseExact(value, "D", out var parsedInstance)
                        || parsedInstance == Guid.Empty)
                    {
                        return (null, "--instance requires a non-empty D-format GUID.");
                    }

                    instanceId = new StudioInstanceId(parsedInstance);
                    break;
                case "--format" when !hasFormat:
                    format = value switch
                    {
                        "text" => StudioObserveOutputFormat.Text,
                        "json" => StudioObserveOutputFormat.Json,
                        _ => (StudioObserveOutputFormat)(-1),
                    };
                    if ((int)format < 0)
                    {
                        return (null, "--format must be 'text' or 'json'.");
                    }

                    hasFormat = true;
                    break;
                case "--timeout-ms" when !hasTimeout:
                    if (!int.TryParse(
                            value,
                            NumberStyles.None,
                            CultureInfo.InvariantCulture,
                            out timeoutMilliseconds)
                        || timeoutMilliseconds <= 0
                        || timeoutMilliseconds
                            > ObservationProtocolLimits.MaxRequestTimeoutMilliseconds)
                    {
                        return (
                            null,
                            $"--timeout-ms must be between 1 and {ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}.");
                    }

                    hasTimeout = true;
                    break;
                case "--after" when !hasAfter:
                    if (!long.TryParse(
                            value,
                            NumberStyles.None,
                            CultureInfo.InvariantCulture,
                            out afterSequence))
                    {
                        return (null, "--after must be a non-negative integer cursor.");
                    }

                    hasAfter = true;
                    break;
                case "--max" when !hasMax:
                    if (!int.TryParse(
                            value,
                            NumberStyles.None,
                            CultureInfo.InvariantCulture,
                            out maxCount)
                        || maxCount <= 0
                        || maxCount > ObservationProtocolLimits.MaxPageSize)
                    {
                        return (
                            null,
                            $"--max must be between 1 and {ObservationProtocolLimits.MaxPageSize}.");
                    }

                    hasMax = true;
                    break;
                case "--channel" when !hasChannel:
                    diagnosticChannel = value switch
                    {
                        "all" => null,
                        "debug" => "debug",
                        "problem" => "problem",
                        _ => "invalid",
                    };
                    if (diagnosticChannel == "invalid")
                    {
                        return (null, "--channel must be 'all', 'debug', or 'problem'.");
                    }

                    hasChannel = true;
                    break;
                case "--window" when !hasWindow:
                    if (!ObservationUiContract.IsValidElementId(value))
                    {
                        return (null, "--window requires a bounded stable UI element ID.");
                    }

                    windowId = value;
                    hasWindow = true;
                    break;
                case "--max-depth" when !hasMaxDepth:
                    if (!int.TryParse(
                            value,
                            NumberStyles.None,
                            CultureInfo.InvariantCulture,
                            out maxDepth)
                        || maxDepth < 0
                        || maxDepth > ObservationProtocolLimits.MaxUiDepth)
                    {
                        return (
                            null,
                            $"--max-depth must be between 0 and {ObservationProtocolLimits.MaxUiDepth}.");
                    }

                    hasMaxDepth = true;
                    break;
                default:
                    return (null, $"Unknown or duplicate option '{option}'.");
            }
        }

        var isCursorRead = verb is StudioObserveVerb.Diagnostics or StudioObserveVerb.Logs;
        var isUiTreeRead = verb == StudioObserveVerb.UiReadTree;
        if (!isCursorRead && (hasAfter || hasChannel))
        {
            return (null, $"{arguments[0]} does not accept cursor options.");
        }

        if (!isCursorRead && !isUiTreeRead && hasMax)
        {
            return (null, $"{arguments[0]} does not accept --max.");
        }

        if (!isUiTreeRead && (hasWindow || hasMaxDepth))
        {
            return (null, $"{arguments[0]} does not accept UI tree options.");
        }

        if (isUiTreeRead && windowId is null)
        {
            return (null, "ui-read-tree requires --window.");
        }

        if (isUiTreeRead && maxCount > ObservationProtocolLimits.MaxUiNodes)
        {
            return (
                null,
                $"ui-read-tree --max must be between 1 and {ObservationProtocolLimits.MaxUiNodes}.");
        }

        if (verb == StudioObserveVerb.Logs && hasChannel)
        {
            return (null, "logs does not accept --channel.");
        }

        if (verb == StudioObserveVerb.List && instanceId is not null)
        {
            return (null, "list does not accept --instance.");
        }

        if (verb != StudioObserveVerb.List && instanceId is null)
        {
            return (null, $"{arguments[0]} requires --instance.");
        }

        return (
            new StudioObserveInvocation(
                verb.Value,
                instanceId,
                format,
                timeoutMilliseconds,
                afterSequence,
                maxCount,
                diagnosticChannel,
                windowId,
                maxDepth),
            null);
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> ListAsync(
        StudioSessionDiscovery discovery,
        StudioObserveOutputFormat format,
        TextWriter output,
        CancellationToken cancellationToken)
    {
        var result = await discovery.ListAsync(cancellationToken)
            .ConfigureAwait(false);
        var sessions = result.Sessions
            .Select(manifest => new SafeStudioSessionSummary(
                manifest.StudioInstanceId.Value.ToString("D"),
                manifest.StudioSessionId.Value.ToString("D"),
                manifest.ProcessId,
                manifest.ProcessStartTimeUtc,
                manifest.EndpointGeneration,
                manifest.BuildIdentity,
                manifest.Configuration,
                manifest.CapabilityDigest,
                manifest.CreatedAtUtc,
                manifest.HeartbeatUtc))
            .ToImmutableArray();
        var issues = result.Issues
            .Select(issue => new SafeDiscoveryIssue(
                issue.ManifestName,
                issue.Failure.Code,
                issue.Failure.Category,
                issue.Failure.Message))
            .ToImmutableArray();
        if (format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(JsonSerializer.Serialize(
                    new SafeSessionListOutput(
                        ObservationProtocolVersion.Current,
                        sessions,
                        issues),
                    OutputJson))
                .ConfigureAwait(false);
        }
        else
        {
            foreach (var session in sessions)
            {
                await output.WriteLineAsync(
                    $"{session.StudioInstanceId} pid={session.ProcessId} generation={session.EndpointGeneration} build={session.BuildIdentity}")
                    .ConfigureAwait(false);
            }

            foreach (var issue in issues)
            {
                await output.WriteLineAsync(
                    $"! {issue.ManifestName} {issue.Code}: {issue.Message}")
                    .ConfigureAwait(false);
            }
        }

        return issues.IsEmpty
            ? (int)StudioObserveExitCode.Success
            : (int)StudioObserveExitCode.Partial;
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> DescribeAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        TextWriter output,
        TextWriter error,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var startedTimestamp = Stopwatch.GetTimestamp();
        var connected = await OpenSessionAsync(
                discovery,
                invocation,
                startedTimestamp,
                operationCancellationToken,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!connected.Succeeded)
        {
            return await WriteFailureAsync(
                connected.Failure ?? new ObservationFailure(
                    "observation.client.failed",
                    "client",
                    "Studio observation client failed without a typed reason.",
                    Retryable: false),
                error);
        }

        await using var connection = connected.Connection!;
        var response = connected.Response!;
        if (invocation.Format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(Encoding.UTF8.GetString(
                    ObservationProtocolJson.WriteResponse(response)))
                .ConfigureAwait(false);
        }
        else
        {
            var descriptor = response.Value!;
            await output.WriteLineAsync(
                $"instance={descriptor.StudioInstanceId.Value:D} session={descriptor.StudioSessionId.Value:D}")
                .ConfigureAwait(false);
            await output.WriteLineAsync(
                $"pid={descriptor.ProcessId} generation={descriptor.EndpointGeneration} state={descriptor.State}")
                .ConfigureAwait(false);
            await output.WriteLineAsync(
                $"build={descriptor.BuildIdentity} configuration={descriptor.Configuration} capabilities={descriptor.Capabilities.Length}")
                .ConfigureAwait(false);
        }

        return (int)StudioObserveExitCode.Success;
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> ReadDiagnosticsAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        TextWriter output,
        TextWriter error,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var startedTimestamp = Stopwatch.GetTimestamp();
        var connected = await OpenSessionAsync(
                discovery,
                invocation,
                startedTimestamp,
                operationCancellationToken,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!connected.Succeeded)
        {
            return await WriteFailureAsync(
                connected.Failure ?? ClientFailed(),
                error);
        }

        await using var connection = connected.Connection!;
        var remaining = Remaining(startedTimestamp, invocation.TimeoutMilliseconds);
        if (remaining <= TimeSpan.Zero)
        {
            return await WriteFailureAsync(ClientTimedOut(), error);
        }

        var result = await connection.ReadDiagnosticsAsync(
                new DiagnosticsReadParameters(
                    invocation.AfterSequence,
                    invocation.MaxCount,
                    invocation.DiagnosticChannel),
                remaining,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!result.Succeeded)
        {
            return await WriteFailureAsync(
                result.Failure ?? ClientFailed(),
                error);
        }

        var response = result.Response!;
        if (invocation.Format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(Encoding.UTF8.GetString(
                    ObservationProtocolJson.WriteResponse(response)))
                .ConfigureAwait(false);
        }
        else
        {
            foreach (var item in response.Value!.Items)
            {
                await output.WriteLineAsync(
                    $"{item.Sequence} {item.TimestampUtc:O} {item.Severity} {item.Channel} {item.Code}: {item.Message}")
                    .ConfigureAwait(false);
            }

            await WriteCursorSummaryAsync(response.Value, output)
                .ConfigureAwait(false);
        }

        return response.Outcome == ObservationOutcome.Partial
            ? (int)StudioObserveExitCode.Partial
            : (int)StudioObserveExitCode.Success;
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> ReadLogsAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        TextWriter output,
        TextWriter error,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var startedTimestamp = Stopwatch.GetTimestamp();
        var connected = await OpenSessionAsync(
                discovery,
                invocation,
                startedTimestamp,
                operationCancellationToken,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!connected.Succeeded)
        {
            return await WriteFailureAsync(
                connected.Failure ?? ClientFailed(),
                error);
        }

        await using var connection = connected.Connection!;
        var remaining = Remaining(startedTimestamp, invocation.TimeoutMilliseconds);
        if (remaining <= TimeSpan.Zero)
        {
            return await WriteFailureAsync(ClientTimedOut(), error);
        }

        var result = await connection.ReadLogsAsync(
                new LogsReadParameters(
                    invocation.AfterSequence,
                    invocation.MaxCount),
                remaining,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!result.Succeeded)
        {
            return await WriteFailureAsync(
                result.Failure ?? ClientFailed(),
                error);
        }

        var response = result.Response!;
        if (invocation.Format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(Encoding.UTF8.GetString(
                    ObservationProtocolJson.WriteResponse(response)))
                .ConfigureAwait(false);
        }
        else
        {
            foreach (var item in response.Value!.Items)
            {
                await output.WriteLineAsync(
                    $"{item.Sequence} {item.TimestampUtc:O} {item.Level} {item.Channel}: {item.RenderedMessage}")
                    .ConfigureAwait(false);
            }

            await WriteCursorSummaryAsync(response.Value, output)
                .ConfigureAwait(false);
        }

        return response.Outcome == ObservationOutcome.Partial
            ? (int)StudioObserveExitCode.Partial
            : (int)StudioObserveExitCode.Success;
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> ListUiWindowsAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        TextWriter output,
        TextWriter error,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var startedTimestamp = Stopwatch.GetTimestamp();
        var connected = await OpenSessionAsync(
                discovery,
                invocation,
                startedTimestamp,
                operationCancellationToken,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!connected.Succeeded)
        {
            return await WriteFailureAsync(connected.Failure ?? ClientFailed(), error);
        }

        await using var connection = connected.Connection!;
        var remaining = Remaining(startedTimestamp, invocation.TimeoutMilliseconds);
        if (remaining <= TimeSpan.Zero)
        {
            return await WriteFailureAsync(ClientTimedOut(), error);
        }

        var result = await connection.ListWindowsAsync(remaining, callerCancellationToken)
            .ConfigureAwait(false);
        if (!result.Succeeded)
        {
            return await WriteFailureAsync(result.Failure ?? ClientFailed(), error);
        }

        var response = result.Response!;
        if (invocation.Format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(Encoding.UTF8.GetString(
                    ObservationProtocolJson.WriteResponse(response)))
                .ConfigureAwait(false);
        }
        else
        {
            await output.WriteLineAsync(
                    $"captured={response.Value!.CapturedAtUtc:O} windows={response.Value.Windows.Length}")
                .ConfigureAwait(false);
            foreach (var window in response.Value.Windows)
            {
                await output.WriteLineAsync(
                        $"window id={window.WindowId} visible={Lower(window.IsVisible)} enabled={Lower(window.IsEnabled)} name={window.Name}")
                    .ConfigureAwait(false);
            }
        }

        return (int)StudioObserveExitCode.Success;
    }

    [SupportedOSPlatform("windows")]
    private static async Task<int> ReadUiTreeAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        TextWriter output,
        TextWriter error,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var startedTimestamp = Stopwatch.GetTimestamp();
        var connected = await OpenSessionAsync(
                discovery,
                invocation,
                startedTimestamp,
                operationCancellationToken,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!connected.Succeeded)
        {
            return await WriteFailureAsync(connected.Failure ?? ClientFailed(), error);
        }

        await using var connection = connected.Connection!;
        var remaining = Remaining(startedTimestamp, invocation.TimeoutMilliseconds);
        if (remaining <= TimeSpan.Zero)
        {
            return await WriteFailureAsync(ClientTimedOut(), error);
        }

        var result = await connection.ReadTreeAsync(
                new UiReadTreeParameters(
                    invocation.WindowId!,
                    invocation.MaxDepth,
                    invocation.MaxCount),
                remaining,
                callerCancellationToken)
            .ConfigureAwait(false);
        if (!result.Succeeded)
        {
            return await WriteFailureAsync(result.Failure ?? ClientFailed(), error);
        }

        var response = result.Response!;
        if (invocation.Format == StudioObserveOutputFormat.Json)
        {
            await output.WriteLineAsync(Encoding.UTF8.GetString(
                    ObservationProtocolJson.WriteResponse(response)))
                .ConfigureAwait(false);
        }
        else
        {
            var tree = response.Value!;
            await output.WriteLineAsync(
                    $"window={tree.WindowId} captured={tree.CapturedAtUtc:O} nodes={tree.Nodes.Length} truncated={Lower(tree.IsTruncated)} reason={tree.TruncationReason ?? "none"}")
                .ConfigureAwait(false);
            foreach (var node in tree.Nodes)
            {
                await output.WriteLineAsync(
                        $"node id={node.ElementId} parent={node.ParentElementId ?? "none"} depth={node.Depth} role={node.Role} visible={Lower(node.IsVisible)} enabled={Lower(node.IsEnabled)} name={node.Name}")
                    .ConfigureAwait(false);
            }
        }

        return response.Outcome == ObservationOutcome.Partial
            ? (int)StudioObserveExitCode.Partial
            : (int)StudioObserveExitCode.Success;
    }

    [SupportedOSPlatform("windows")]
    private static async ValueTask<StudioObservationConnectResult> OpenSessionAsync(
        StudioSessionDiscovery discovery,
        StudioObserveInvocation invocation,
        long startedTimestamp,
        CancellationToken operationCancellationToken,
        CancellationToken callerCancellationToken)
    {
        var resolution = await discovery.ResolveAsync(
                invocation.StudioInstanceId!.Value,
                operationCancellationToken)
            .ConfigureAwait(false);
        if (resolution.Manifest is null)
        {
            return new StudioObservationConnectResult(
                Connection: null,
                Response: null,
                resolution.Failure ?? new ObservationFailure(
                    "observation.discovery.invalid",
                    "protocol",
                    "Studio discovery manifest was rejected.",
                    Retryable: false));
        }

        var remaining = Remaining(startedTimestamp, invocation.TimeoutMilliseconds);
        if (remaining <= TimeSpan.Zero)
        {
            return new StudioObservationConnectResult(
                Connection: null,
                Response: null,
                ClientTimedOut());
        }

        return await StudioObservationClient.ConnectAsync(
                resolution.Manifest,
                remaining,
                callerCancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask WriteCursorSummaryAsync<T>(
        ObservationCursorWindow<T> window,
        TextWriter output)
        where T : class
    {
        await output.WriteLineAsync(
            $"cursor oldest={window.OldestAvailableSequence} next={window.NextCursor} dropped={window.TotalDropped} expired={window.CursorExpired.ToString().ToLowerInvariant()} truncated={window.Truncated.ToString().ToLowerInvariant()}")
            .ConfigureAwait(false);
    }

    private static TimeSpan Remaining(long startedTimestamp, int timeoutMilliseconds)
    {
        var timeout = TimeSpan.FromMilliseconds(timeoutMilliseconds);
        var elapsed = Stopwatch.GetElapsedTime(startedTimestamp);
        return elapsed >= timeout ? TimeSpan.Zero : timeout - elapsed;
    }

    private static string Lower(bool value) => value.ToString().ToLowerInvariant();

    private static ObservationFailure ClientFailed() =>
        new(
            "observation.client.failed",
            "client",
            "Studio observation client failed without a typed reason.",
            Retryable: false);

    private static ObservationFailure ClientTimedOut() =>
        new(
            "observation.client.timed-out",
            "operation",
            "Studio observation command exhausted its deadline.",
            Retryable: true);

    private static async Task<int> WriteFailureAsync(
        ObservationFailure failure,
        TextWriter error)
    {
        await error.WriteLineAsync($"{failure.Code}: {failure.Message}")
            .ConfigureAwait(false);
        return (int)MapFailure(failure);
    }

    private static StudioObserveExitCode MapFailure(ObservationFailure failure)
    {
        if (failure.Code.Contains("timed-out", StringComparison.Ordinal))
        {
            return StudioObserveExitCode.TimedOut;
        }

        if (failure.Code.Contains("cancelled", StringComparison.Ordinal))
        {
            return StudioObserveExitCode.Cancelled;
        }

        return failure.Category switch
        {
            "protocol" => StudioObserveExitCode.Protocol,
            "security" => StudioObserveExitCode.Authorization,
            "stale" => StudioObserveExitCode.Stale,
            "unavailable" => StudioObserveExitCode.Unavailable,
            _ => StudioObserveExitCode.Failed,
        };
    }

    private static string Usage() =>
        "usage: asharia-studio-observe list [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe describe --instance <guid> [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe diagnostics --instance <guid> [--after N] [--max N] [--channel all|debug|problem] [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe logs --instance <guid> [--after N] [--max N] [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe ui-list-windows --instance <guid> [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe ui-read-tree --instance <guid> --window <id> [--max-depth N] [--max N] [--format text|json] [--timeout-ms N]\n"
        + "       asharia-studio-observe mcp";
}

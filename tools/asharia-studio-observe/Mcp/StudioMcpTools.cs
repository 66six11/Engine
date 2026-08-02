using System;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.Client;
using Asharia.Studio.Observe.Discovery;

namespace Asharia.Studio.Observe.Mcp;

internal sealed record StudioMcpToolDefinition(
    string Name,
    string Title,
    string Description,
    JsonElement InputSchema);

internal sealed record StudioMcpToolExecutionResult(
    JsonElement StructuredContent,
    bool IsError);

internal sealed record StudioMcpSessionSummary(
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

internal sealed record StudioMcpDiscoveryIssue(
    string ManifestName,
    string Code,
    string Category,
    string Message);

internal sealed record StudioMcpSessionListResult(
    ObservationProtocolVersion Protocol,
    string Outcome,
    ImmutableArray<StudioMcpSessionSummary> Sessions,
    ImmutableArray<StudioMcpDiscoveryIssue> Issues);

internal sealed record StudioMcpFailureResult(
    string Outcome,
    ObservationFailure Failure);

internal static class StudioMcpTools
{
    internal const int DefaultTimeoutMilliseconds = 2_000;

    private static readonly JsonSerializerOptions StructuredJson = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        MaxDepth = ObservationProtocolLimits.MaxJsonDepth,
    };

    internal static ImmutableArray<StudioMcpToolDefinition> Definitions { get; } =
    [
        Tool(
            "studio_list_sessions",
            "List Studio sessions",
            "List bounded current-user Asharia Studio development sessions without exposing attach tokens.",
            $$"""
            {
              "type":"object",
              "properties":{
                "timeoutMilliseconds":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}},"default":{{DefaultTimeoutMilliseconds}}}
              },
              "additionalProperties":false
            }
            """),
        Tool(
            "studio_describe_session",
            "Describe Studio session",
            "Attach to one explicit Studio instance and read its typed session and capability descriptor.",
            SessionSchema()),
        Tool(
            "studio_read_diagnostics",
            "Read Studio diagnostics",
            "Read one bounded diagnostic cursor window from the Studio process truth.",
            $$"""
            {
              "type":"object",
              "properties":{
                "instanceId":{"type":"string","format":"uuid"},
                "afterSequence":{"type":"integer","minimum":0,"default":0},
                "maxCount":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxPageSize}},"default":100},
                "channel":{"type":"string","enum":["all","debug","problem"],"default":"all"},
                "timeoutMilliseconds":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}},"default":{{DefaultTimeoutMilliseconds}}}
              },
              "required":["instanceId"],
              "additionalProperties":false
            }
            """),
        Tool(
            "studio_read_logs",
            "Read Studio logs",
            "Read one bounded log cursor window from the Studio process truth.",
            $$"""
            {
              "type":"object",
              "properties":{
                "instanceId":{"type":"string","format":"uuid"},
                "afterSequence":{"type":"integer","minimum":0,"default":0},
                "maxCount":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxPageSize}},"default":100},
                "timeoutMilliseconds":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}},"default":{{DefaultTimeoutMilliseconds}}}
              },
              "required":["instanceId"],
              "additionalProperties":false
            }
            """),
        Tool(
            "studio_list_ui_windows",
            "List Studio UI windows",
            "Read the bounded semantic window list advertised by one explicit Studio instance.",
            SessionSchema()),
        Tool(
            "studio_read_ui_tree",
            "Read Studio UI tree",
            "Read one bounded immutable semantic UI tree; this cannot inspect arbitrary properties or send input.",
            $$"""
            {
              "type":"object",
              "properties":{
                "instanceId":{"type":"string","format":"uuid"},
                "windowId":{"type":"string","minLength":1,"maxLength":{{ObservationProtocolLimits.MaxUiElementIdCharacters}},"pattern":"^[A-Za-z][A-Za-z0-9._-]*$"},
                "maxDepth":{"type":"integer","minimum":0,"maximum":{{ObservationProtocolLimits.MaxUiDepth}},"default":{{ObservationProtocolLimits.MaxUiDepth}}},
                "maxNodes":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxUiNodes}},"default":{{ObservationProtocolLimits.MaxUiNodes}}},
                "timeoutMilliseconds":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}},"default":{{DefaultTimeoutMilliseconds}}}
              },
              "required":["instanceId","windowId"],
              "additionalProperties":false
            }
            """),
    ];

    [SupportedOSPlatform("windows")]
    internal static async ValueTask<StudioMcpToolExecutionResult> ExecuteAsync(
        string toolName,
        JsonElement arguments,
        StudioSessionDiscovery discovery,
        CancellationToken cancellationToken)
    {
        var parsed = ParseArguments(toolName, arguments);
        if (parsed.Failure is not null)
        {
            return Failure(parsed.Failure);
        }

        var request = parsed.Value!;
        using var deadline = new CancellationTokenSource(
            TimeSpan.FromMilliseconds(request.TimeoutMilliseconds));
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadline.Token);
        try
        {
            if (toolName == "studio_list_sessions")
            {
                var listed = await discovery.ListAsync(operation.Token)
                    .ConfigureAwait(false);
                var sessions = listed.Sessions
                    .Select(manifest => new StudioMcpSessionSummary(
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
                var issues = listed.Issues
                    .Select(issue => new StudioMcpDiscoveryIssue(
                        issue.ManifestName,
                        issue.Failure.Code,
                        issue.Failure.Category,
                        issue.Failure.Message))
                    .ToImmutableArray();
                return Success(JsonSerializer.SerializeToElement(
                    new StudioMcpSessionListResult(
                        ObservationProtocolVersion.Current,
                        issues.IsEmpty ? "complete" : "partial",
                        sessions,
                        issues),
                    StructuredJson));
            }

            var resolution = await discovery.ResolveAsync(
                    request.StudioInstanceId!.Value,
                    operation.Token)
                .ConfigureAwait(false);
            if (resolution.Manifest is null)
            {
                return Failure(resolution.Failure ?? ClientFailure(
                    "observation.discovery.invalid",
                    "protocol",
                    "Studio discovery manifest was rejected."));
            }

            var connected = await StudioObservationClient.ConnectAsync(
                    resolution.Manifest,
                    TimeSpan.FromMilliseconds(request.TimeoutMilliseconds),
                    operation.Token)
                .ConfigureAwait(false);
            if (!connected.Succeeded)
            {
                return Failure(NormalizeCancellation(
                    connected.Failure ?? ClientFailure(
                        "observation.client.failed",
                        "client",
                        "Studio observation client failed without a typed reason."),
                    cancellationToken,
                    deadline));
            }

            await using var connection = connected.Connection!;
            if (toolName == "studio_describe_session")
            {
                return Success(ProtocolResponse(connected.Response!));
            }

            return toolName switch
            {
                "studio_read_diagnostics" => FromOperation(await connection
                    .ReadDiagnosticsAsync(
                        new DiagnosticsReadParameters(
                            request.AfterSequence,
                            request.MaxCount,
                            request.Channel),
                        TimeSpan.FromMilliseconds(request.TimeoutMilliseconds),
                        operation.Token)
                    .ConfigureAwait(false), cancellationToken, deadline),
                "studio_read_logs" => FromOperation(await connection
                    .ReadLogsAsync(
                        new LogsReadParameters(
                            request.AfterSequence,
                            request.MaxCount),
                        TimeSpan.FromMilliseconds(request.TimeoutMilliseconds),
                        operation.Token)
                    .ConfigureAwait(false), cancellationToken, deadline),
                "studio_list_ui_windows" => FromOperation(await connection
                    .ListWindowsAsync(
                        TimeSpan.FromMilliseconds(request.TimeoutMilliseconds),
                        operation.Token)
                    .ConfigureAwait(false), cancellationToken, deadline),
                "studio_read_ui_tree" => FromOperation(await connection
                    .ReadTreeAsync(
                        new UiReadTreeParameters(
                            request.WindowId!,
                            request.MaxDepth,
                            request.MaxNodes),
                        TimeSpan.FromMilliseconds(request.TimeoutMilliseconds),
                        operation.Token)
                    .ConfigureAwait(false), cancellationToken, deadline),
                _ => Failure(ClientFailure(
                    "mcp.tool.unknown",
                    "protocol",
                    "The requested MCP tool is not registered.")),
            };
        }
        catch (OperationCanceledException)
        {
            return Failure(CancellationFailure(cancellationToken, deadline));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return Failure(ClientFailure(
                "observation.discovery.unavailable",
                "unavailable",
                "Studio discovery is unavailable."));
        }
    }

    private static StudioMcpToolExecutionResult FromOperation<TValue>(
        StudioObservationOperationResult<TValue> operation,
        CancellationToken cancellationToken,
        CancellationTokenSource deadline)
        where TValue : class =>
        operation.Succeeded
            ? Success(ProtocolResponse(operation.Response!))
            : Failure(NormalizeCancellation(
                operation.Failure ?? ClientFailure(
                    "observation.client.failed",
                    "client",
                    "Studio observation client failed without a typed reason."),
                cancellationToken,
                deadline));

    private static JsonElement ProtocolResponse<TValue>(
        ObservationResponse<TValue> response)
        where TValue : class
    {
        using var document = JsonDocument.Parse(
            ObservationProtocolJson.WriteResponse(response),
            new JsonDocumentOptions
            {
                MaxDepth = ObservationProtocolLimits.MaxJsonDepth,
            });
        return document.RootElement.Clone();
    }

    private static StudioMcpToolExecutionResult Success(JsonElement value) =>
        new(value, IsError: false);

    private static StudioMcpToolExecutionResult Failure(ObservationFailure failure) =>
        new(
            JsonSerializer.SerializeToElement(
                new StudioMcpFailureResult(
                    "failed",
                    failure.Attributes.IsDefault
                        ? failure with { Attributes = [] }
                        : failure),
                StructuredJson),
            IsError: true);

    private static ObservationFailure NormalizeCancellation(
        ObservationFailure failure,
        CancellationToken caller,
        CancellationTokenSource deadline)
    {
        if (!failure.Code.Contains("cancelled", StringComparison.Ordinal))
        {
            return failure;
        }

        return CancellationFailure(caller, deadline);
    }

    private static ObservationFailure CancellationFailure(
        CancellationToken caller,
        CancellationTokenSource deadline) =>
        caller.IsCancellationRequested
            ? ClientFailure(
                "observation.client.cancelled",
                "operation",
                "Studio observation request was cancelled.",
                retryable: true)
            : deadline.IsCancellationRequested
                ? ClientFailure(
                    "observation.client.timed-out",
                    "operation",
                    "Studio observation request exceeded its MCP tool deadline.",
                    retryable: true)
                : ClientFailure(
                    "observation.client.cancelled",
                    "operation",
                    "Studio observation request was cancelled.",
                    retryable: true);

    private static ObservationFailure ClientFailure(
        string code,
        string category,
        string message,
        bool retryable = false) =>
        new(code, category, message, retryable);

    private static (StudioMcpArguments? Value, ObservationFailure? Failure) ParseArguments(
        string toolName,
        JsonElement arguments)
    {
        if (arguments.ValueKind != JsonValueKind.Object)
        {
            return Rejected("Tool arguments must be a JSON object.");
        }

        var argumentNames = new System.Collections.Generic.HashSet<string>(
            StringComparer.Ordinal);
        if (arguments.EnumerateObject().Any(property => !argumentNames.Add(property.Name)))
        {
            return Rejected("Tool arguments contain duplicate fields.");
        }

        StudioInstanceId? instanceId = null;
        var timeoutMilliseconds = DefaultTimeoutMilliseconds;
        long afterSequence = 0;
        var maxCount = 100;
        string? channel = null;
        string? windowId = null;
        var maxDepth = ObservationProtocolLimits.MaxUiDepth;
        var maxNodes = ObservationProtocolLimits.MaxUiNodes;
        foreach (var property in arguments.EnumerateObject())
        {
            switch (property.Name)
            {
                case "instanceId":
                    if (property.Value.ValueKind != JsonValueKind.String
                        || !Guid.TryParseExact(property.Value.GetString(), "D", out var instanceGuid)
                        || instanceGuid == Guid.Empty)
                    {
                        return Rejected("instanceId must be one non-empty canonical UUID.");
                    }

                    instanceId = new StudioInstanceId(instanceGuid);
                    break;
                case "timeoutMilliseconds":
                    if (!property.Value.TryGetInt32(out timeoutMilliseconds)
                        || timeoutMilliseconds <= 0
                        || timeoutMilliseconds > ObservationProtocolLimits.MaxRequestTimeoutMilliseconds)
                    {
                        return Rejected("timeoutMilliseconds is outside the typed protocol limit.");
                    }

                    break;
                case "afterSequence":
                    if (!property.Value.TryGetInt64(out afterSequence)
                        || afterSequence < 0)
                    {
                        return Rejected("afterSequence must be a non-negative integer.");
                    }

                    break;
                case "maxCount":
                    if (!property.Value.TryGetInt32(out maxCount)
                        || maxCount <= 0
                        || maxCount > ObservationProtocolLimits.MaxPageSize)
                    {
                        return Rejected("maxCount is outside the typed protocol page limit.");
                    }

                    break;
                case "channel":
                    channel = property.Value.ValueKind == JsonValueKind.String
                        ? property.Value.GetString()
                        : null;
                    if (channel is not "all" and not "debug" and not "problem")
                    {
                        return Rejected("channel must be all, debug, or problem.");
                    }

                    break;
                case "windowId":
                    windowId = property.Value.ValueKind == JsonValueKind.String
                        ? property.Value.GetString()
                        : null;
                    if (!ObservationUiContract.IsValidElementId(windowId))
                    {
                        return Rejected("windowId must be one stable bounded semantic ID.");
                    }

                    break;
                case "maxDepth":
                    if (!property.Value.TryGetInt32(out maxDepth)
                        || maxDepth < 0
                        || maxDepth > ObservationProtocolLimits.MaxUiDepth)
                    {
                        return Rejected("maxDepth is outside the typed UI depth limit.");
                    }

                    break;
                case "maxNodes":
                    if (!property.Value.TryGetInt32(out maxNodes)
                        || maxNodes <= 0
                        || maxNodes > ObservationProtocolLimits.MaxUiNodes)
                    {
                        return Rejected("maxNodes is outside the typed UI node limit.");
                    }

                    break;
                default:
                    return Rejected($"Unknown tool argument '{property.Name}'.");
            }
        }

        var allowed = toolName switch
        {
            "studio_list_sessions" => new[] { "timeoutMilliseconds" },
            "studio_describe_session" or "studio_list_ui_windows" =>
                ["instanceId", "timeoutMilliseconds"],
            "studio_read_diagnostics" =>
                ["instanceId", "afterSequence", "maxCount", "channel", "timeoutMilliseconds"],
            "studio_read_logs" =>
                ["instanceId", "afterSequence", "maxCount", "timeoutMilliseconds"],
            "studio_read_ui_tree" =>
                ["instanceId", "windowId", "maxDepth", "maxNodes", "timeoutMilliseconds"],
            _ => Array.Empty<string>(),
        };
        if (arguments.EnumerateObject().Any(property => !allowed.Contains(
                property.Name,
                StringComparer.Ordinal)))
        {
            return Rejected("Tool arguments do not match the selected fixed tool schema.");
        }

        if (toolName != "studio_list_sessions" && instanceId is null)
        {
            return Rejected("This tool requires one explicit instanceId.");
        }

        if (toolName == "studio_read_ui_tree" && windowId is null)
        {
            return Rejected("studio_read_ui_tree requires one explicit windowId.");
        }

        channel = channel == "all" ? null : channel;
        return (
            new StudioMcpArguments(
                instanceId,
                timeoutMilliseconds,
                afterSequence,
                maxCount,
                channel,
                windowId,
                maxDepth,
                maxNodes),
            Failure: null);
    }

    private static (StudioMcpArguments? Value, ObservationFailure? Failure) Rejected(
        string message) =>
        (
            Value: null,
            ClientFailure(
                "mcp.tool.invalid-arguments",
                "protocol",
                message));

    private static StudioMcpToolDefinition Tool(
        string name,
        string title,
        string description,
        string schema) =>
        new(name, title, description, ParseJson(schema));

    private static string SessionSchema() =>
        $$"""
        {
          "type":"object",
          "properties":{
            "instanceId":{"type":"string","format":"uuid"},
            "timeoutMilliseconds":{"type":"integer","minimum":1,"maximum":{{ObservationProtocolLimits.MaxRequestTimeoutMilliseconds}},"default":{{DefaultTimeoutMilliseconds}}}
          },
          "required":["instanceId"],
          "additionalProperties":false
        }
        """;

    private static JsonElement ParseJson(string json)
    {
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }

    private sealed record StudioMcpArguments(
        StudioInstanceId? StudioInstanceId,
        int TimeoutMilliseconds,
        long AfterSequence,
        int MaxCount,
        string? Channel,
        string? WindowId,
        int MaxDepth,
        int MaxNodes);
}

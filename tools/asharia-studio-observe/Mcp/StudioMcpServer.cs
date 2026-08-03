using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.Discovery;

namespace Asharia.Studio.Observe.Mcp;

internal static class StudioMcpServer
{
    internal const string ProtocolVersion = "2025-06-18";
    internal const int MaxInflightRequests = 8;
    internal const int MaxInputBytes = ObservationProtocolLimits.MaxRequestBytes;
    internal const int MaxOutputBytes = ObservationProtocolLimits.MaxResponseBytes;

    private const int ServerBusyError = -31_000;
    private const int ParseError = -32_700;
    private const int InvalidRequest = -32_600;
    private const int MethodNotFound = -32_601;
    private const int InvalidParams = -32_602;
    private const int InternalError = -32_603;
    private static readonly JsonDocumentOptions DocumentOptions = new()
    {
        CommentHandling = JsonCommentHandling.Disallow,
        MaxDepth = ObservationProtocolLimits.MaxJsonDepth,
    };
    private static readonly JsonElement EmptyObject = ParseJson("{}");

    [SupportedOSPlatform("windows")]
    internal static Task<int> RunStandardIoAsync(CancellationToken cancellationToken) =>
        RunAsync(
            Console.OpenStandardInput(),
            Console.OpenStandardOutput(),
            new StudioSessionDiscovery(),
            cancellationToken);

    [SupportedOSPlatform("windows")]
    internal static async Task<int> RunAsync(
        Stream input,
        Stream output,
        StudioSessionDiscovery discovery,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(discovery);
        using var lifetime = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        using var writer = new StudioMcpMessageWriter(output);
        var reader = new StudioMcpLineReader(input, MaxInputBytes);
        var inflight = new ConcurrentDictionary<
            StudioMcpRequestKey,
            StudioMcpInflightRequest>();
        var lifecycle = StudioMcpLifecycleState.AwaitingInitialize;
        try
        {
            while (!lifetime.IsCancellationRequested)
            {
                StudioMcpLine line;
                try
                {
                    line = await reader.ReadAsync(lifetime.Token).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                if (line.Status == StudioMcpLineStatus.EndOfStream)
                {
                    break;
                }

                if (line.Status != StudioMcpLineStatus.Complete)
                {
                    await writer.WriteErrorAsync(
                            id: null,
                            ParseError,
                            line.Status == StudioMcpLineStatus.TooLarge
                                ? "MCP message exceeds the bounded input limit."
                                : "MCP message is not valid UTF-8.",
                            data: null,
                            lifetime.Token)
                        .ConfigureAwait(false);
                    continue;
                }

                JsonDocument document;
                try
                {
                    document = JsonDocument.Parse(line.Json!, DocumentOptions);
                }
                catch (JsonException)
                {
                    await writer.WriteErrorAsync(
                            id: null,
                            ParseError,
                            "MCP message is not valid bounded JSON.",
                            data: null,
                            lifetime.Token)
                        .ConfigureAwait(false);
                    continue;
                }

                using (document)
                {
                    var root = document.RootElement;
                    if (!TryReadMessage(root, out var id, out var method, out var isNotification))
                    {
                        await writer.WriteErrorAsync(
                                id: null,
                                InvalidRequest,
                                "MCP message is not a valid JSON-RPC 2.0 request or notification.",
                                data: null,
                                lifetime.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    if (isNotification)
                    {
                        lifecycle = HandleNotification(
                            root,
                            method!,
                            lifecycle,
                            inflight);
                        continue;
                    }

                    if (string.Equals(method, "initialize", StringComparison.Ordinal))
                    {
                        if (lifecycle != StudioMcpLifecycleState.AwaitingInitialize)
                        {
                            await writer.WriteErrorAsync(
                                    id,
                                    InvalidRequest,
                                    "MCP connection is already initialized.",
                                    data: null,
                                    lifetime.Token)
                                .ConfigureAwait(false);
                            continue;
                        }

                        var initializationFailure = ValidateInitializeRequest(root);
                        if (initializationFailure is not null)
                        {
                            await writer.WriteErrorAsync(
                                    id,
                                    InvalidParams,
                                    initializationFailure,
                                    data: null,
                                    lifetime.Token)
                                .ConfigureAwait(false);
                            continue;
                        }

                        await writer.WriteInitializeAsync(id!.Value, lifetime.Token)
                            .ConfigureAwait(false);
                        lifecycle = StudioMcpLifecycleState.AwaitingInitializedNotification;
                        continue;
                    }

                    if (lifecycle == StudioMcpLifecycleState.AwaitingInitialize)
                    {
                        await writer.WriteErrorAsync(
                                id,
                                InvalidRequest,
                                "MCP initialize must be the first request.",
                                data: null,
                                lifetime.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    if (lifecycle == StudioMcpLifecycleState.AwaitingInitializedNotification
                        && !string.Equals(method, "ping", StringComparison.Ordinal))
                    {
                        await writer.WriteErrorAsync(
                                id,
                                InvalidRequest,
                                "MCP client must send notifications/initialized before tool requests.",
                                data: null,
                                lifetime.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    _ = TryGetRequestKey(id!.Value, out var requestKey);
                    if (inflight.Count >= MaxInflightRequests)
                    {
                        await writer.WriteErrorAsync(
                                id,
                                ServerBusyError,
                                "MCP server reached its bounded in-flight request limit.",
                                new { maxInflightRequests = MaxInflightRequests },
                                lifetime.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    var requestCancellation = CancellationTokenSource
                        .CreateLinkedTokenSource(lifetime.Token);
                    var trackedRequest = new StudioMcpInflightRequest(requestCancellation);
                    if (!inflight.TryAdd(requestKey, trackedRequest))
                    {
                        requestCancellation.Dispose();
                        await writer.WriteErrorAsync(
                                id,
                                InvalidRequest,
                                "MCP request ID is already in flight.",
                                data: null,
                                lifetime.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    var request = root.Clone();
                    var requestId = id.Value.Clone();
                    trackedRequest.Completion = CompleteRequestAsync(
                        ProcessRequestAsync(
                            request,
                            requestId,
                            method!,
                            discovery,
                            writer,
                            requestCancellation.Token,
                            lifetime.Token).AsTask(),
                        requestKey,
                        trackedRequest,
                        inflight);
                }
            }
        }
        finally
        {
            lifetime.Cancel();
            foreach (var request in inflight.Values)
            {
                request.Cancellation.Cancel();
            }

            try
            {
                await Task.WhenAll(inflight.Values
                        .Select(request => request.Completion)
                        .Where(task => task is not null)
                        .Cast<Task>())
                    .WaitAsync(TimeSpan.FromSeconds(5))
                    .ConfigureAwait(false);
            }
            catch (Exception error) when (error is OperationCanceledException or TimeoutException)
            {
                // EOF/owner cancellation is the portable stdio teardown signal. The process
                // must not wait indefinitely for a broken downstream endpoint.
            }
        }

        return cancellationToken.IsCancellationRequested ? 130 : 0;
    }

    [SupportedOSPlatform("windows")]
    private static async ValueTask ProcessRequestAsync(
        JsonElement request,
        JsonElement id,
        string method,
        StudioSessionDiscovery discovery,
        StudioMcpMessageWriter writer,
        CancellationToken requestCancellationToken,
        CancellationToken lifetimeCancellationToken)
    {
        try
        {
            switch (method)
            {
                case "tools/list":
                    if (!HasOptionalParameters(request, "_meta"))
                    {
                        await writer.WriteErrorAsync(
                                id,
                                InvalidParams,
                                "tools/list has no cursor because the fixed tool set fits one page.",
                                data: null,
                                requestCancellationToken,
                                lifetimeCancellationToken)
                            .ConfigureAwait(false);
                        return;
                    }

                    await writer.WriteToolListAsync(
                            id,
                            requestCancellationToken,
                            lifetimeCancellationToken)
                        .ConfigureAwait(false);
                    return;
                case "ping":
                    if (!HasOptionalParameters(request, "_meta"))
                    {
                        await writer.WriteErrorAsync(
                                id,
                                InvalidParams,
                                "ping accepts no operation parameters.",
                                data: null,
                                requestCancellationToken,
                                lifetimeCancellationToken)
                            .ConfigureAwait(false);
                        return;
                    }

                    await writer.WriteEmptyResultAsync(
                            id,
                            requestCancellationToken,
                            lifetimeCancellationToken)
                        .ConfigureAwait(false);
                    return;
                case "tools/call":
                    await ProcessToolCallAsync(
                            request,
                            id,
                            discovery,
                            writer,
                            requestCancellationToken,
                            lifetimeCancellationToken)
                        .ConfigureAwait(false);
                    return;
                default:
                    await writer.WriteErrorAsync(
                            id,
                            MethodNotFound,
                            "MCP method is not implemented by this tools-only server.",
                            data: null,
                            requestCancellationToken,
                            lifetimeCancellationToken)
                        .ConfigureAwait(false);
                    return;
            }
        }
        catch (OperationCanceledException) when (requestCancellationToken.IsCancellationRequested)
        {
            // notifications/cancelled forbids any later response for this request.
        }
        catch (Exception)
        {
            if (!requestCancellationToken.IsCancellationRequested
                && !lifetimeCancellationToken.IsCancellationRequested)
            {
                await writer.WriteErrorAsync(
                        id,
                        InternalError,
                        "MCP request failed inside the bounded adapter.",
                        data: null,
                        requestCancellationToken,
                        lifetimeCancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

    [SupportedOSPlatform("windows")]
    private static async ValueTask ProcessToolCallAsync(
        JsonElement request,
        JsonElement id,
        StudioSessionDiscovery discovery,
        StudioMcpMessageWriter writer,
        CancellationToken requestCancellationToken,
        CancellationToken lifetimeCancellationToken)
    {
        if (!TryGetParameters(request, out var parameters)
            || !HasOnlyProperties(parameters, "_meta", "name", "arguments")
            || !HasValidOptionalMetadata(parameters)
            || !parameters.TryGetProperty("name", out var nameElement)
            || nameElement.ValueKind != JsonValueKind.String)
        {
            await writer.WriteErrorAsync(
                    id,
                    InvalidParams,
                    "tools/call requires one fixed tool name and an optional arguments object.",
                    data: null,
                    requestCancellationToken,
                    lifetimeCancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var name = nameElement.GetString()!;
        if (!StudioMcpTools.Definitions.Any(definition => string.Equals(
                definition.Name,
                name,
                StringComparison.Ordinal)))
        {
            await writer.WriteErrorAsync(
                    id,
                    InvalidParams,
                    $"Unknown tool: {name}",
                    data: null,
                    requestCancellationToken,
                    lifetimeCancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var arguments = parameters.TryGetProperty("arguments", out var argumentsElement)
            ? argumentsElement
            : EmptyObject;
        if (arguments.ValueKind != JsonValueKind.Object)
        {
            await writer.WriteErrorAsync(
                    id,
                    InvalidParams,
                    "tools/call arguments must be a JSON object.",
                    data: null,
                    requestCancellationToken,
                    lifetimeCancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var result = await StudioMcpTools.ExecuteAsync(
                name,
                arguments,
                discovery,
                requestCancellationToken)
            .ConfigureAwait(false);
        if (requestCancellationToken.IsCancellationRequested)
        {
            return;
        }

        await writer.WriteToolResultAsync(
                id,
                result,
                requestCancellationToken,
                lifetimeCancellationToken)
            .ConfigureAwait(false);
    }

    private static async Task CompleteRequestAsync(
        Task requestTask,
        StudioMcpRequestKey requestKey,
        StudioMcpInflightRequest request,
        ConcurrentDictionary<StudioMcpRequestKey, StudioMcpInflightRequest> inflight)
    {
        try
        {
            await requestTask.ConfigureAwait(false);
        }
        finally
        {
            inflight.TryRemove(requestKey, out _);
            request.Cancellation.Dispose();
        }
    }

    private static StudioMcpLifecycleState HandleNotification(
        JsonElement request,
        string method,
        StudioMcpLifecycleState lifecycle,
        ConcurrentDictionary<StudioMcpRequestKey, StudioMcpInflightRequest> inflight)
    {
        if (string.Equals(method, "notifications/initialized", StringComparison.Ordinal))
        {
            return lifecycle == StudioMcpLifecycleState.AwaitingInitializedNotification
                && HasOptionalParameters(request, "_meta")
                    ? StudioMcpLifecycleState.Operating
                    : lifecycle;
        }

        if (string.Equals(method, "notifications/cancelled", StringComparison.Ordinal)
            && TryGetParameters(request, out var parameters)
            && HasOnlyProperties(parameters, "_meta", "requestId", "reason")
            && HasValidOptionalMetadata(parameters)
            && parameters.TryGetProperty("requestId", out var requestId)
            && TryGetRequestKey(requestId, out var requestKey)
            && inflight.TryGetValue(requestKey, out var operation))
        {
            operation.Cancellation.Cancel();
        }

        return lifecycle;
    }

    private static string? ValidateInitializeRequest(JsonElement request)
    {
        if (!TryGetParameters(request, out var parameters)
            || !HasOnlyProperties(
                parameters,
                "_meta",
                "protocolVersion",
                "capabilities",
                "clientInfo")
            || !parameters.TryGetProperty("protocolVersion", out var version)
            || version.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(version.GetString())
            || !parameters.TryGetProperty("capabilities", out var capabilities)
            || capabilities.ValueKind != JsonValueKind.Object
            || !HasUniqueProperties(capabilities)
            || !parameters.TryGetProperty("clientInfo", out var clientInfo)
            || clientInfo.ValueKind != JsonValueKind.Object
            || !HasUniqueProperties(clientInfo)
            || !clientInfo.TryGetProperty("name", out var name)
            || name.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(name.GetString())
            || !clientInfo.TryGetProperty("version", out var clientVersion)
            || clientVersion.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(clientVersion.GetString()))
        {
            return "initialize requires protocolVersion, capabilities, and clientInfo name/version.";
        }

        if (!HasValidOptionalMetadata(parameters))
        {
            return "initialize _meta must be an unambiguous JSON object when present.";
        }

        return null;
    }

    private static bool TryReadMessage(
        JsonElement root,
        out JsonElement? id,
        out string? method,
        out bool isNotification)
    {
        id = null;
        method = null;
        isNotification = false;
        if (root.ValueKind != JsonValueKind.Object
            || !HasUniqueProperties(root)
            || !root.TryGetProperty("jsonrpc", out var jsonRpc)
            || jsonRpc.ValueKind != JsonValueKind.String
            || !string.Equals(jsonRpc.GetString(), "2.0", StringComparison.Ordinal)
            || !root.TryGetProperty("method", out var methodElement)
            || methodElement.ValueKind != JsonValueKind.String
            || string.IsNullOrWhiteSpace(methodElement.GetString()))
        {
            return false;
        }

        method = methodElement.GetString();
        if (!root.TryGetProperty("id", out var idElement))
        {
            isNotification = true;
            return true;
        }

        if (!IsValidId(idElement))
        {
            return false;
        }

        id = idElement;
        return true;
    }

    private static bool IsValidId(JsonElement id) =>
        TryGetRequestKey(id, out _);

    private static bool TryGetRequestKey(
        JsonElement id,
        out StudioMcpRequestKey requestKey)
    {
        if (id.ValueKind == JsonValueKind.String
            && id.GetString() is { } stringValue)
        {
            requestKey = new StudioMcpRequestKey(
                StudioMcpRequestIdKind.String,
                stringValue,
                IntegerValue: 0);
            return true;
        }

        if (id.ValueKind == JsonValueKind.Number
            && id.TryGetInt64(out var integerValue))
        {
            requestKey = new StudioMcpRequestKey(
                StudioMcpRequestIdKind.Integer,
                StringValue: null,
                integerValue);
            return true;
        }

        requestKey = default;
        return false;
    }

    private static bool HasOptionalParameters(
        JsonElement request,
        params string[] allowed)
    {
        if (!request.TryGetProperty("params", out var parameters))
        {
            return true;
        }

        return parameters.ValueKind == JsonValueKind.Object
            && HasOnlyProperties(parameters, allowed)
            && HasValidOptionalMetadata(parameters);
    }

    private static bool TryGetParameters(
        JsonElement request,
        out JsonElement parameters)
    {
        if (request.TryGetProperty("params", out parameters)
            && parameters.ValueKind == JsonValueKind.Object)
        {
            return true;
        }

        parameters = default;
        return false;
    }

    private static bool HasOnlyProperties(
        JsonElement value,
        params string[] allowed) =>
        HasUniqueProperties(value)
        && value.EnumerateObject().All(property => allowed.Contains(
            property.Name,
            StringComparer.Ordinal));

    private static bool HasValidOptionalMetadata(JsonElement value) =>
        !value.TryGetProperty("_meta", out var metadata)
        || (metadata.ValueKind == JsonValueKind.Object
            && HasUniqueProperties(metadata));

    private static bool HasUniqueProperties(JsonElement value)
    {
        var names = new HashSet<string>(StringComparer.Ordinal);
        return value.EnumerateObject().All(property => names.Add(property.Name));
    }

    private static JsonElement ParseJson(string json)
    {
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }

    private enum StudioMcpRequestIdKind
    {
        String,
        Integer,
    }

    private readonly record struct StudioMcpRequestKey(
        StudioMcpRequestIdKind Kind,
        string? StringValue,
        long IntegerValue);
}

internal enum StudioMcpLifecycleState
{
    AwaitingInitialize,
    AwaitingInitializedNotification,
    Operating,
}

internal sealed class StudioMcpInflightRequest
{
    internal StudioMcpInflightRequest(CancellationTokenSource cancellation)
    {
        Cancellation = cancellation;
    }

    internal CancellationTokenSource Cancellation { get; }

    internal Task? Completion { get; set; }
}

internal enum StudioMcpLineStatus
{
    Complete,
    EndOfStream,
    TooLarge,
    InvalidUtf8,
}

internal sealed record StudioMcpLine(
    StudioMcpLineStatus Status,
    string? Json = null);

internal sealed class StudioMcpLineReader
{
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private readonly Stream input_;
    private readonly int maxBytes_;
    private readonly byte[] buffer_ = new byte[8_192];
    private int start_;
    private int end_;

    internal StudioMcpLineReader(Stream input, int maxBytes)
    {
        input_ = input;
        maxBytes_ = maxBytes;
    }

    internal async ValueTask<StudioMcpLine> ReadAsync(
        CancellationToken cancellationToken)
    {
        using var line = new MemoryStream(capacity: 8_192);
        var tooLarge = false;
        while (true)
        {
            var newline = Array.IndexOf(buffer_, (byte)'\n', start_, end_ - start_);
            if (newline >= 0)
            {
                Append(buffer_.AsSpan(start_, newline - start_), line, ref tooLarge);
                start_ = newline + 1;
                return Decode(line, tooLarge);
            }

            Append(buffer_.AsSpan(start_, end_ - start_), line, ref tooLarge);
            start_ = 0;
            end_ = await input_.ReadAsync(buffer_, cancellationToken)
                .ConfigureAwait(false);
            if (end_ == 0)
            {
                return line.Length == 0 && !tooLarge
                    ? new StudioMcpLine(StudioMcpLineStatus.EndOfStream)
                    : Decode(line, tooLarge);
            }
        }
    }

    private void Append(
        ReadOnlySpan<byte> bytes,
        MemoryStream line,
        ref bool tooLarge)
    {
        if (tooLarge)
        {
            return;
        }

        if (line.Length + bytes.Length > maxBytes_)
        {
            tooLarge = true;
            return;
        }

        line.Write(bytes);
    }

    private static StudioMcpLine Decode(MemoryStream line, bool tooLarge)
    {
        if (tooLarge)
        {
            return new StudioMcpLine(StudioMcpLineStatus.TooLarge);
        }

        var bytes = line.GetBuffer().AsSpan(0, checked((int)line.Length));
        if (!bytes.IsEmpty && bytes[^1] == (byte)'\r')
        {
            bytes = bytes[..^1];
        }

        try
        {
            return new StudioMcpLine(
                StudioMcpLineStatus.Complete,
                StrictUtf8.GetString(bytes));
        }
        catch (DecoderFallbackException)
        {
            return new StudioMcpLine(StudioMcpLineStatus.InvalidUtf8);
        }
    }
}

internal sealed class StudioMcpMessageWriter : IDisposable
{
    private readonly Stream output_;
    private readonly SemaphoreSlim gate_ = new(1, 1);

    internal StudioMcpMessageWriter(Stream output)
    {
        output_ = output;
    }

    internal ValueTask WriteInitializeAsync(
        JsonElement id,
        CancellationToken cancellationToken) =>
        WriteResultAsync(
            id,
            writer =>
            {
                writer.WriteString("protocolVersion", StudioMcpServer.ProtocolVersion);
                WriteCapabilities(writer);
                writer.WritePropertyName("serverInfo");
                writer.WriteStartObject();
                writer.WriteString("name", "asharia-studio-observe");
                writer.WriteString("title", "Asharia Studio Observe");
                writer.WriteString("version", "1.0.0");
                writer.WriteEndObject();
                writer.WriteString(
                    "instructions",
                    "Read-only Asharia Studio observation. Select an explicit instance; cursors and partial/truncation fields are authoritative. No mutation, input, capture, or arbitrary RPC is available.");
            },
            cancellationToken,
            cancellationToken);

    internal ValueTask WriteToolListAsync(
        JsonElement id,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken) =>
        WriteResultAsync(
            id,
            writer =>
            {
                writer.WritePropertyName("tools");
                writer.WriteStartArray();
                foreach (var definition in StudioMcpTools.Definitions)
                {
                    writer.WriteStartObject();
                    writer.WriteString("name", definition.Name);
                    writer.WriteString("title", definition.Title);
                    writer.WriteString("description", definition.Description);
                    writer.WritePropertyName("inputSchema");
                    definition.InputSchema.WriteTo(writer);
                    writer.WritePropertyName("annotations");
                    writer.WriteStartObject();
                    writer.WriteBoolean("readOnlyHint", true);
                    writer.WriteBoolean("destructiveHint", false);
                    writer.WriteBoolean("idempotentHint", true);
                    writer.WriteBoolean("openWorldHint", false);
                    writer.WriteEndObject();
                    writer.WriteEndObject();
                }

                writer.WriteEndArray();
            },
            gateCancellationToken,
            writeCancellationToken);

    internal ValueTask WriteEmptyResultAsync(
        JsonElement id,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken) =>
        WriteResultAsync(
            id,
            _ => { },
            gateCancellationToken,
            writeCancellationToken);

    internal async ValueTask WriteToolResultAsync(
        JsonElement id,
        StudioMcpToolExecutionResult result,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken)
    {
        var payload = BuildResponse(
            id,
            writer =>
            {
                writer.WritePropertyName("content");
                writer.WriteStartArray();
                writer.WriteStartObject();
                writer.WriteString("type", "text");
                writer.WriteString("text", result.StructuredContent.GetRawText());
                writer.WriteEndObject();
                writer.WriteEndArray();
                writer.WritePropertyName("structuredContent");
                result.StructuredContent.WriteTo(writer);
                writer.WriteBoolean("isError", result.IsError);
            });
        if (payload.Length > StudioMcpServer.MaxOutputBytes)
        {
            var failure = JsonSerializer.SerializeToElement(new
            {
                outcome = "failed",
                failure = new
                {
                    code = "mcp.response.too-large",
                    category = "protocol",
                    message = "MCP tool result exceeds the bounded output limit.",
                    retryable = false,
                },
            });
            payload = BuildResponse(
                id,
                writer =>
                {
                    writer.WritePropertyName("content");
                    writer.WriteStartArray();
                    writer.WriteStartObject();
                    writer.WriteString("type", "text");
                    writer.WriteString("text", failure.GetRawText());
                    writer.WriteEndObject();
                    writer.WriteEndArray();
                    writer.WritePropertyName("structuredContent");
                    failure.WriteTo(writer);
                    writer.WriteBoolean("isError", true);
                });
        }

        await WritePayloadAsync(
                payload,
                gateCancellationToken,
                writeCancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask WriteErrorAsync(
        JsonElement? id,
        int code,
        string message,
        object? data,
        CancellationToken cancellationToken) =>
        WriteErrorAsync(
            id,
            code,
            message,
            data,
            cancellationToken,
            cancellationToken);

    internal ValueTask WriteErrorAsync(
        JsonElement? id,
        int code,
        string message,
        object? data,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteString("jsonrpc", "2.0");
            writer.WritePropertyName("id");
            if (id is { } responseId)
            {
                responseId.WriteTo(writer);
            }
            else
            {
                writer.WriteNullValue();
            }

            writer.WritePropertyName("error");
            writer.WriteStartObject();
            writer.WriteNumber("code", code);
            writer.WriteString("message", message);
            if (data is not null)
            {
                writer.WritePropertyName("data");
                JsonSerializer.Serialize(writer, data);
            }

            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        return WritePayloadAsync(
            buffer.WrittenMemory,
            gateCancellationToken,
            writeCancellationToken);
    }

    public void Dispose() => gate_.Dispose();

    private ValueTask WriteResultAsync(
        JsonElement id,
        Action<Utf8JsonWriter> writeResult,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken) =>
        WritePayloadAsync(
            BuildResponse(id, writeResult),
            gateCancellationToken,
            writeCancellationToken);

    private static byte[] BuildResponse(
        JsonElement id,
        Action<Utf8JsonWriter> writeResult)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteString("jsonrpc", "2.0");
            writer.WritePropertyName("id");
            id.WriteTo(writer);
            writer.WritePropertyName("result");
            writer.WriteStartObject();
            writeResult(writer);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        return buffer.WrittenSpan.ToArray();
    }

    private async ValueTask WritePayloadAsync(
        ReadOnlyMemory<byte> payload,
        CancellationToken gateCancellationToken,
        CancellationToken writeCancellationToken)
    {
        if (payload.Length > StudioMcpServer.MaxOutputBytes)
        {
            throw new InvalidDataException("MCP response exceeds its bounded output limit.");
        }

        await gate_.WaitAsync(gateCancellationToken).ConfigureAwait(false);
        try
        {
            gateCancellationToken.ThrowIfCancellationRequested();
            await output_.WriteAsync(payload, writeCancellationToken).ConfigureAwait(false);
            await output_.WriteAsync("\n"u8.ToArray(), writeCancellationToken)
                .ConfigureAwait(false);
            await output_.FlushAsync(writeCancellationToken).ConfigureAwait(false);
        }
        finally
        {
            gate_.Release();
        }
    }

    private static void WriteCapabilities(Utf8JsonWriter writer)
    {
        writer.WritePropertyName("capabilities");
        writer.WriteStartObject();
        writer.WritePropertyName("tools");
        writer.WriteStartObject();
        writer.WriteBoolean("listChanged", false);
        writer.WriteEndObject();
        writer.WriteEndObject();
    }

}

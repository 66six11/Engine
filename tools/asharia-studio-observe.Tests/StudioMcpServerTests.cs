using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.Discovery;
using Asharia.Studio.Observe.Mcp;
using Xunit;

namespace Asharia.Studio.Observe.Tests;

[SupportedOSPlatform("windows")]
public sealed class StudioMcpServerTests
{
    [Fact]
    public async Task Standard_initialize_and_tool_list_are_exact_bounded_read_only_surface()
    {
        await using var server = StartUninitializedServer();

        var initialize = await server.RequestAsync(InitializeRequest(1));
        var initializeResult = initialize.RootElement.GetProperty("result");
        Assert.Equal(
            StudioMcpServer.ProtocolVersion,
            initializeResult.GetProperty("protocolVersion").GetString());
        Assert.Equal(
            "asharia-studio-observe",
            initializeResult.GetProperty("serverInfo").GetProperty("name").GetString());
        Assert.False(initializeResult
            .GetProperty("capabilities")
            .GetProperty("tools")
            .GetProperty("listChanged")
            .GetBoolean());

        var premature = await server.RequestAsync(Request(2, "tools/list"));
        Assert.Equal(-32600, ErrorCode(premature));

        await server.SendAsync(InitializedNotification());
        var listed = await server.RequestAsync(Request(3, "tools/list"));
        var tools = listed.RootElement
            .GetProperty("result")
            .GetProperty("tools")
            .EnumerateArray()
            .ToArray();
        Assert.Equal(
            [
                "studio_list_sessions",
                "studio_describe_session",
                "studio_read_diagnostics",
                "studio_read_logs",
                "studio_list_ui_windows",
                "studio_read_ui_tree",
            ],
            tools.Select(tool => tool.GetProperty("name").GetString()!).ToArray());
        Assert.All(tools, tool =>
        {
            Assert.Equal("object", tool.GetProperty("inputSchema").GetProperty("type").GetString());
            Assert.False(tool.GetProperty("inputSchema").GetProperty("additionalProperties").GetBoolean());
            var annotations = tool.GetProperty("annotations");
            Assert.True(annotations.GetProperty("readOnlyHint").GetBoolean());
            Assert.False(annotations.GetProperty("destructiveHint").GetBoolean());
            Assert.True(annotations.GetProperty("idempotentHint").GetBoolean());
            Assert.False(annotations.GetProperty("openWorldHint").GetBoolean());
        });
        var uiTree = Assert.Single(tools, tool =>
            tool.GetProperty("name").GetString() == "studio_read_ui_tree");
        var properties = uiTree.GetProperty("inputSchema").GetProperty("properties");
        Assert.Equal(
            ObservationProtocolLimits.MaxUiDepth,
            properties.GetProperty("maxDepth").GetProperty("maximum").GetInt32());
        Assert.Equal(
            ObservationProtocolLimits.MaxUiNodes,
            properties.GetProperty("maxNodes").GetProperty("maximum").GetInt32());

        await server.StopAsync();
    }

    [Fact]
    public async Task Protocol_lifecycle_negotiates_version_and_rejects_invalid_requests()
    {
        await using var server = StartUninitializedServer();

        var malformed = await server.RequestAsync("{]");
        Assert.Equal(-32700, ErrorCode(malformed));

        var beforeInitialize = await server.RequestAsync(Request(2, "tools/list"));
        Assert.Equal(-32600, ErrorCode(beforeInitialize));

        var invalidInitialize = await server.RequestAsync(
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"initialize\",\"params\":{}}");
        Assert.Equal(-32602, ErrorCode(invalidInitialize));

        var negotiated = await server.RequestAsync(InitializeRequest(
            4,
            version: "2025-11-25"));
        Assert.Equal(
            StudioMcpServer.ProtocolVersion,
            negotiated.RootElement
                .GetProperty("result")
                .GetProperty("protocolVersion")
                .GetString());
        await server.SendAsync(InitializedNotification());

        var duplicateInitialize = await server.RequestAsync(InitializeRequest(5));
        Assert.Equal(-32600, ErrorCode(duplicateInitialize));

        var unknownTool = await server.RequestAsync(ToolRequest(
            6,
            "studio_read_state",
            "{}"));
        Assert.Equal(-32602, ErrorCode(unknownTool));

        var invalidArguments = await server.RequestAsync(ToolRequest(
            7,
            "studio_read_logs",
            "{\"maxCount\":1001}"));
        var invalidResult = invalidArguments.RootElement.GetProperty("result");
        Assert.True(invalidResult.GetProperty("isError").GetBoolean());
        Assert.Equal(
            "mcp.tool.invalid-arguments",
            invalidResult.GetProperty("structuredContent")
                .GetProperty("failure")
                .GetProperty("code")
                .GetString());

        var oversized = await server.RequestAsync(
            new string('x', StudioMcpServer.MaxInputBytes + 1));
        Assert.Equal(-32700, ErrorCode(oversized));

        await server.StopAsync();
    }

    [Fact]
    public async Task Standard_metadata_and_duplicate_nested_fields_fail_closed()
    {
        await using var server = StartUninitializedServer();

        var invalidInitializeMetadata = await server.RequestAsync(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
            + $"\"protocolVersion\":\"{StudioMcpServer.ProtocolVersion}\","
            + "\"capabilities\":{},\"clientInfo\":{\"name\":\"asharia-tests\",\"version\":\"1.0\"},"
            + "\"_meta\":\"invalid\"}}");
        Assert.Equal(-32602, ErrorCode(invalidInitializeMetadata));

        var duplicateCapabilities = await server.RequestAsync(
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\",\"params\":{"
            + $"\"protocolVersion\":\"{StudioMcpServer.ProtocolVersion}\","
            + "\"capabilities\":{\"roots\":{},\"roots\":{}},"
            + "\"clientInfo\":{\"name\":\"asharia-tests\",\"version\":\"1.0\"}}}");
        Assert.Equal(-32602, ErrorCode(duplicateCapabilities));

        using var initialized = await server.RequestAsync(InitializeRequest(3));
        Assert.Equal(StudioMcpServer.ProtocolVersion, initialized.RootElement
            .GetProperty("result")
            .GetProperty("protocolVersion")
            .GetString());

        await server.SendAsync(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\","
            + "\"params\":{\"_meta\":[]}}");
        var stillAwaitingInitialized = await server.RequestAsync(Request(4, "tools/list"));
        Assert.Equal(-32600, ErrorCode(stillAwaitingInitialized));

        await server.SendAsync(InitializedNotification());
        var invalidListMetadata = await server.RequestAsync(
            "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/list\","
            + "\"params\":{\"_meta\":false}}");
        Assert.Equal(-32602, ErrorCode(invalidListMetadata));

        var invalidCallMetadata = await server.RequestAsync(
            "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{"
            + "\"name\":\"studio_list_sessions\",\"arguments\":{},"
            + "\"_meta\":{\"trace\":1,\"trace\":2}}}");
        Assert.Equal(-32602, ErrorCode(invalidCallMetadata));

        await server.StopAsync();
    }

    [Fact]
    public async Task Real_endpoint_all_six_tools_preserve_typed_partial_semantics_and_secret_redaction()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        PublishDiagnostic(hub, "studio.mcp.problem");
        PublishLog(hub, "MCP log 1.");
        PublishLog(hub, "MCP log 2.");
        PublishLog(hub, "MCP log 3.");
        await using var host = CreateHost(hub, new FixedUiObservationSource());
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifest = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath)).Value!;
        var instanceId = host.StudioInstanceId.Value.ToString("D");
        await using var server = await StartServerAsync();
        var payloads = new List<string>();

        var listed = await server.RequestAsync(ToolRequest(
            1,
            "studio_list_sessions",
            "{}"));
        payloads.Add(listed.RootElement.GetRawText());
        Assert.Contains(
            listed.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("sessions")
                .EnumerateArray(),
            session => session.GetProperty("studioInstanceId").GetString() == instanceId);

        var described = await server.RequestAsync(ToolRequest(
            2,
            "studio_describe_session",
            $"{{\"instanceId\":\"{instanceId}\"}}"));
        payloads.Add(described.RootElement.GetRawText());
        Assert.Equal(
            instanceId,
            described.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("value")
                .GetProperty("studioInstanceId")
                .GetString());

        var diagnostics = await server.RequestAsync(ToolRequest(
            3,
            "studio_read_diagnostics",
            $"{{\"instanceId\":\"{instanceId}\",\"channel\":\"problem\",\"maxCount\":1}}"));
        payloads.Add(diagnostics.RootElement.GetRawText());
        Assert.Equal(
            "studio.mcp.problem",
            diagnostics.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("value")
                .GetProperty("items")[0]
                .GetProperty("code")
                .GetString());

        var logs = await server.RequestAsync(ToolRequest(
            4,
            "studio_read_logs",
            $"{{\"instanceId\":\"{instanceId}\",\"maxCount\":1}}"));
        payloads.Add(logs.RootElement.GetRawText());
        var logsResponse = logs.RootElement.GetProperty("result");
        Assert.False(logsResponse.GetProperty("isError").GetBoolean());
        Assert.Equal(
            "partial",
            logsResponse.GetProperty("structuredContent").GetProperty("outcome").GetString());
        Assert.True(logsResponse.GetProperty("structuredContent")
            .GetProperty("value")
            .GetProperty("cursorExpired")
            .GetBoolean());

        var windows = await server.RequestAsync(ToolRequest(
            5,
            "studio_list_ui_windows",
            $"{{\"instanceId\":\"{instanceId}\"}}"));
        payloads.Add(windows.RootElement.GetRawText());
        Assert.Equal(
            "StudioShellWindow",
            windows.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("value")
                .GetProperty("windows")[0]
                .GetProperty("windowId")
                .GetString());

        var tree = await server.RequestAsync(ToolRequest(
            6,
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"maxNodes\":1}}"));
        payloads.Add(tree.RootElement.GetRawText());
        Assert.Equal(
            "partial",
            tree.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("outcome")
                .GetString());

        Assert.DoesNotContain(
            manifest.AttachToken,
            string.Join('\n', payloads),
            StringComparison.Ordinal);
        await server.StopAsync();
    }

    [Fact]
    public async Task Cursor_contradiction_is_a_typed_mcp_tool_error()
    {
        var hub = new ContradictoryCursorHub();
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");

        var response = await server.RequestAsync(ToolRequest(
            1,
            "studio_read_logs",
            $"{{\"instanceId\":\"{instanceId}\",\"afterSequence\":0,\"maxCount\":1}}"));

        var result = response.RootElement.GetProperty("result");
        Assert.True(result.GetProperty("isError").GetBoolean());
        Assert.Equal(
            "observation.client.invalid-cursor",
            result.GetProperty("structuredContent")
                .GetProperty("failure")
                .GetProperty("code")
                .GetString());

        await server.StopAsync();
    }

    [Fact]
    public async Task Cancel_notification_stops_real_inflight_tool_without_late_response()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var source = new ControlledUiObservationSource();
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");

        await server.SendAsync(ToolRequest(
            1,
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":30000}}"));
        Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(5)));
        await server.SendAsync(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1,\"reason\":\"test\"}}");
        var ping = await server.RequestAsync(Request(2, "ping"));
        Assert.Equal(2, ping.RootElement.GetProperty("id").GetInt32());
        Assert.Empty(ping.RootElement.GetProperty("result").EnumerateObject());
        source.Release();
        Assert.True(source.Completed.Wait(TimeSpan.FromSeconds(5)));
        await server.StopAsync();
    }

    [Fact]
    public async Task Cancel_while_waiting_for_the_output_gate_suppresses_the_response()
    {
        await using var input = new QueuedInputStream();
        await using var output = new BlockingOutputStream();
        var run = StudioMcpServer.RunAsync(
            input,
            output,
            new StudioSessionDiscovery(),
            CancellationToken.None);

        try
        {
            await input.WaitForReadRequestAsync();
            input.Send(InitializeRequest(0));
            Assert.Equal(0, await output.ReadResponseIdAsync());
            await input.WaitForReadRequestAsync();
            input.Send(InitializedNotification());
            await input.WaitForReadRequestAsync();
            input.Send(Request(1, "ping"));
            await Task.WhenAll(
                output.BlockedWriteEntered.WaitAsync(TimeSpan.FromSeconds(5)),
                input.WaitForReadRequestAsync());

            input.Send(Request(2, "ping"));
            await input.WaitForReadRequestAsync();
            input.Send(
                "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\","
                + "\"params\":{\"requestId\":2,\"reason\":\"writer-gate-test\"}}");
            await input.WaitForReadRequestAsync();
            input.Send(Request(3, "ping"));
            await input.WaitForReadRequestAsync();

            output.Release();
            var responseIds = new List<int>();
            while (!responseIds.Contains(3))
            {
                responseIds.Add(await output.ReadResponseIdAsync());
            }

            Assert.Equal([1, 3], responseIds);
        }
        finally
        {
            output.Release();
            input.Complete();
            Assert.Equal(0, await run.WaitAsync(TimeSpan.FromSeconds(5)));
        }
    }

    [Fact]
    public async Task Escaped_string_id_cancels_and_releases_the_same_semantic_request_id()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var source = new ControlledUiObservationSource();
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");

        await server.SendAsync(ToolRequest(
            "alpha",
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":30000}}"));
        Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(5)));
        await server.SendAsync(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":\"\\u0061lpha\",\"reason\":\"semantic-id-test\"}}");

        JsonDocument? reuse = null;
        for (var attempt = 0; attempt < 50; attempt++)
        {
            reuse = await server.RequestAsync(Request("alpha", "ping"));
            if (!reuse.RootElement.TryGetProperty("error", out _))
            {
                break;
            }

            Assert.Equal(-32600, ErrorCode(reuse));
            reuse.Dispose();
            reuse = null;
            await Task.Delay(20);
        }

        using (reuse)
        {
            Assert.NotNull(reuse);
            Assert.Empty(reuse.RootElement.GetProperty("result").EnumerateObject());
        }

        source.Release();
        Assert.True(source.Completed.Wait(TimeSpan.FromSeconds(5)));
        await server.StopAsync();
    }

    [Fact]
    public async Task Tool_deadline_returns_typed_timeout_without_stopping_the_studio_owner()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var source = new ControlledUiObservationSource();
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");

        await server.SendAsync(ToolRequest(
            1,
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":1000}}"));
        Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(5)));
        var response = await server.ReadAsync();

        Assert.True(response.RootElement.GetProperty("result").GetProperty("isError").GetBoolean());
        Assert.Equal(
            "observation.client.timed-out",
            response.RootElement.GetProperty("result")
                .GetProperty("structuredContent")
                .GetProperty("failure")
                .GetProperty("code")
                .GetString());
        Assert.Equal(StudioDevelopmentHostState.Running, host.State);
        source.Release();
        Assert.True(source.Completed.Wait(TimeSpan.FromSeconds(5)));
        await server.StopAsync();
    }

    [Fact]
    public async Task Duplicate_id_and_ninth_inflight_request_fail_closed_at_fixed_capacity()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var source = new ControlledUiObservationSource();
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");
        var blockedCall = ToolRequest(
            1,
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":30000}}");

        await server.SendAsync(blockedCall);
        Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(5)));
        var duplicate = await server.RequestAsync(Request(1, "ping"));
        Assert.Equal(-32600, ErrorCode(duplicate));

        for (var id = 2; id <= StudioMcpServer.MaxInflightRequests; ++id)
        {
            await server.SendAsync(ToolRequest(
                id,
                "studio_read_ui_tree",
                $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":30000}}"));
        }

        var busy = await server.RequestAsync(Request(9, "ping"));
        Assert.Equal(-31000, ErrorCode(busy));
        Assert.Equal(
            StudioMcpServer.MaxInflightRequests,
            busy.RootElement.GetProperty("error")
                .GetProperty("data")
                .GetProperty("maxInflightRequests")
                .GetInt32());

        for (var id = 1; id <= StudioMcpServer.MaxInflightRequests; ++id)
        {
            await server.SendAsync(
                $"{{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{{\"requestId\":{id}}}}}");
        }

        source.Release();
        Assert.True(source.Completed.Wait(TimeSpan.FromSeconds(5)));
        await server.StopAsync();
    }

    [Fact]
    public async Task Stdin_eof_cancels_real_inflight_tool_and_exits_cleanly()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var source = new ControlledUiObservationSource();
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        await using var server = await StartServerAsync();
        var instanceId = host.StudioInstanceId.Value.ToString("D");

        await server.SendAsync(ToolRequest(
            1,
            "studio_read_ui_tree",
            $"{{\"instanceId\":\"{instanceId}\",\"windowId\":\"StudioShellWindow\",\"timeoutMilliseconds\":30000}}"));
        Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(5)));

        var started = Stopwatch.GetTimestamp();
        await server.StopAsync();

        Assert.True(Stopwatch.GetElapsedTime(started) < TimeSpan.FromSeconds(5));
        Assert.Equal(0, server.ExitCode);
        source.Release();
        Assert.True(source.Completed.Wait(TimeSpan.FromSeconds(5)));
    }

    private static async Task<McpProcess> StartServerAsync()
    {
        var server = StartUninitializedServer();
        try
        {
            await server.InitializeAsync();
            return server;
        }
        catch
        {
            await server.DisposeAsync();
            throw;
        }
    }

    private static McpProcess StartUninitializedServer()
    {
        var assemblyPath = typeof(Asharia.Studio.Observe.Program).Assembly.Location;
        var process = Process.Start(new ProcessStartInfo
        {
            FileName = "dotnet",
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            ArgumentList =
            {
                assemblyPath,
                "mcp",
            },
        }) ?? throw new InvalidOperationException("Failed to start MCP child process.");
        return new McpProcess(process);
    }

    private static string Request<TId>(
        TId id,
        string method) =>
        JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["method"] = method,
            ["params"] = new Dictionary<string, object?>(),
        });

    private static string InitializeRequest<TId>(
        TId id,
        string version = StudioMcpServer.ProtocolVersion) =>
        JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["method"] = "initialize",
            ["params"] = new Dictionary<string, object?>
            {
                ["protocolVersion"] = version,
                ["capabilities"] = new { },
                ["clientInfo"] = new
                {
                    name = "asharia-tests",
                    version = "1.0",
                },
            },
        });

    private static string InitializedNotification() =>
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

    private static string ToolRequest<TId>(
        TId id,
        string name,
        string arguments)
    {
        using var document = JsonDocument.Parse(arguments);
        return JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["jsonrpc"] = "2.0",
            ["id"] = id,
            ["method"] = "tools/call",
            ["params"] = new Dictionary<string, object?>
            {
                ["name"] = name,
                ["arguments"] = document.RootElement.Clone(),
            },
        });
    }

    private static int ErrorCode(JsonDocument response) =>
        response.RootElement.GetProperty("error").GetProperty("code").GetInt32();

    private static StudioDevelopmentHost CreateHost(
        IStudioDiagnosticHub hub,
        IStudioUiObservationSource? uiObservationSource = null) =>
        StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioMcpServerTests).Module.ModuleVersionId:D}",
            "Test",
            endpointGeneration: 23,
            providerGeneration: 2,
            uiObservationSource);

    private static void PublishDiagnostic(StudioDiagnosticHub hub, string code) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            StudioDiagnosticChannel.Problem,
            code,
            "test",
            Context(hub, "diagnostics"),
            code));

    private static void PublishLog(StudioDiagnosticHub hub, string message) =>
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "mcp",
            Context(hub, "logs"),
            "MCP log {Sequence}.",
            message));

    private static StudioDiagnosticContext Context(
        IStudioDiagnosticHub hub,
        string component) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.tests",
            component,
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private sealed class FixedUiObservationSource : IStudioUiObservationSource
    {
        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ObservationProtocolReadResult<UiWindowListResult>(
                new UiWindowListResult(
                    DateTimeOffset.UtcNow,
                    [new ObservationUiWindow("StudioShellWindow", "Asharia Studio", true, true)]),
                Failure: null));
        }

        public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var truncated = parameters.MaxNodes == 1;
            return ValueTask.FromResult(new ObservationProtocolReadResult<UiTreeReadResult>(
                new UiTreeReadResult(
                    parameters.WindowId,
                    DateTimeOffset.UtcNow,
                    truncated,
                    truncated ? "ui.max-nodes" : null,
                    [
                        new ObservationUiNode(
                            parameters.WindowId,
                            ParentElementId: null,
                            Depth: 0,
                            "Asharia Studio",
                            ObservationUiRoles.Window,
                            IsVisible: true,
                            IsEnabled: true),
                    ]),
                Failure: null));
        }
    }

    private sealed class ControlledUiObservationSource : IStudioUiObservationSource, IDisposable
    {
        private readonly TaskCompletionSource release_ = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public ManualResetEventSlim Entered { get; } = new(initialState: false);

        public ManualResetEventSlim Completed { get; } = new(initialState: false);

        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ObservationProtocolReadResult<UiWindowListResult>(
                new UiWindowListResult(DateTimeOffset.UtcNow, []),
                Failure: null));

        public async ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken)
        {
            Entered.Set();
            try
            {
                await release_.Task.WaitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                throw;
            }

            Completed.Set();
            return new ObservationProtocolReadResult<UiTreeReadResult>(
                new UiTreeReadResult(
                    parameters.WindowId,
                    DateTimeOffset.UtcNow,
                    IsTruncated: false,
                    TruncationReason: null,
                    [
                        new ObservationUiNode(
                            parameters.WindowId,
                            ParentElementId: null,
                            Depth: 0,
                            "Asharia Studio",
                            ObservationUiRoles.Window,
                            IsVisible: true,
                            IsEnabled: true),
                    ]),
                Failure: null);
        }

        public void Release() => release_.TrySetResult();

        public void Dispose()
        {
            release_.TrySetResult();
            Entered.Dispose();
            Completed.Dispose();
        }
    }

    private sealed class McpProcess : IAsyncDisposable
    {
        private readonly Process process_;
        private bool stopped_;

        internal McpProcess(Process process)
        {
            process_ = process;
        }

        internal int ExitCode => process_.ExitCode;

        internal async Task InitializeAsync()
        {
            using var response = await RequestAsync(InitializeRequest(0));
            Assert.Equal(
                StudioMcpServer.ProtocolVersion,
                response.RootElement
                    .GetProperty("result")
                    .GetProperty("protocolVersion")
                    .GetString());
            await SendAsync(InitializedNotification());
        }

        internal async Task SendAsync(string message)
        {
            await process_.StandardInput.WriteLineAsync(message);
            await process_.StandardInput.FlushAsync();
        }

        internal async Task<JsonDocument> RequestAsync(string message)
        {
            await SendAsync(message);
            return await ReadAsync();
        }

        internal async Task<JsonDocument> ReadAsync()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            var line = await process_.StandardOutput.ReadLineAsync(deadline.Token);
            Assert.False(string.IsNullOrWhiteSpace(line));
            return JsonDocument.Parse(line!);
        }

        internal async Task StopAsync()
        {
            if (stopped_)
            {
                return;
            }

            stopped_ = true;
            process_.StandardInput.Close();
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            try
            {
                await process_.WaitForExitAsync(deadline.Token);
                var stderr = await process_.StandardError.ReadToEndAsync(deadline.Token);
                Assert.Equal(0, process_.ExitCode);
                Assert.Equal(string.Empty, stderr);
            }
            catch
            {
                if (!process_.HasExited)
                {
                    process_.Kill(entireProcessTree: true);
                    await process_.WaitForExitAsync();
                }

                throw;
            }
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                await StopAsync();
            }
            finally
            {
                process_.Dispose();
            }
        }
    }

    private sealed class QueuedInputStream : Stream
    {
        private readonly Channel<byte[]> messages_ = Channel.CreateUnbounded<byte[]>();
        private readonly SemaphoreSlim readRequests_ = new(initialCount: 0);
        private byte[]? current_;
        private int offset_;

        internal void Send(string message)
        {
            if (!messages_.Writer.TryWrite(Encoding.UTF8.GetBytes(message + "\n")))
            {
                throw new InvalidOperationException("MCP probe input is closed.");
            }
        }

        internal void Complete() => messages_.Writer.TryComplete();

        internal async Task WaitForReadRequestAsync()
        {
            if (!await readRequests_.WaitAsync(TimeSpan.FromSeconds(5)))
            {
                throw new TimeoutException("MCP server did not request its next input frame.");
            }
        }

        public override async ValueTask<int> ReadAsync(
            Memory<byte> buffer,
            CancellationToken cancellationToken = default)
        {
            while (current_ is null || offset_ == current_.Length)
            {
                if (!messages_.Reader.TryRead(out current_))
                {
                    readRequests_.Release();
                    if (!await messages_.Reader.WaitToReadAsync(cancellationToken))
                    {
                        return 0;
                    }

                    continue;
                }

                offset_ = 0;
            }

            var count = Math.Min(buffer.Length, current_.Length - offset_);
            current_.AsMemory(offset_, count).CopyTo(buffer);
            offset_ += count;
            return count;
        }

        public override bool CanRead => true;

        public override bool CanSeek => false;

        public override bool CanWrite => false;

        public override long Length => throw new NotSupportedException();

        public override long Position
        {
            get => throw new NotSupportedException();
            set => throw new NotSupportedException();
        }

        public override void Flush()
        {
        }

        public override int Read(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();

        public override long Seek(long offset, SeekOrigin origin) =>
            throw new NotSupportedException();

        public override void SetLength(long value) =>
            throw new NotSupportedException();

        public override void Write(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                Complete();
                readRequests_.Dispose();
            }

            base.Dispose(disposing);
        }
    }

    private sealed class BlockingOutputStream : Stream
    {
        private readonly Channel<int> responseIds_ = Channel.CreateUnbounded<int>();
        private readonly MemoryStream currentFrame_ = new();
        private readonly TaskCompletionSource blockedWriteEntered_ = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource release_ = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        private int blockedWrite_;

        internal Task BlockedWriteEntered => blockedWriteEntered_.Task;

        internal void Release() => release_.TrySetResult();

        internal async Task<int> ReadResponseIdAsync()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            return await responseIds_.Reader.ReadAsync(deadline.Token);
        }

        public override async ValueTask WriteAsync(
            ReadOnlyMemory<byte> buffer,
            CancellationToken cancellationToken = default)
        {
            if (buffer.Span.SequenceEqual("\n"u8))
            {
                using var document = JsonDocument.Parse(currentFrame_.ToArray());
                await responseIds_.Writer.WriteAsync(
                    document.RootElement.GetProperty("id").GetInt32(),
                    cancellationToken);
                currentFrame_.SetLength(0);
                return;
            }

            using (var response = JsonDocument.Parse(buffer))
            {
                var responseId = response.RootElement.GetProperty("id");
                if (responseId.ValueKind == JsonValueKind.Number
                    && responseId.GetInt32() == 1
                    && Interlocked.CompareExchange(ref blockedWrite_, 1, 0) == 0)
                {
                    blockedWriteEntered_.TrySetResult();
                    await release_.Task.WaitAsync(cancellationToken);
                }
            }

            await currentFrame_.WriteAsync(buffer, cancellationToken);
        }

        public override bool CanRead => false;

        public override bool CanSeek => false;

        public override bool CanWrite => true;

        public override long Length => currentFrame_.Length;

        public override long Position
        {
            get => currentFrame_.Position;
            set => throw new NotSupportedException();
        }

        public override void Flush()
        {
        }

        public override int Read(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();

        public override long Seek(long offset, SeekOrigin origin) =>
            throw new NotSupportedException();

        public override void SetLength(long value) =>
            throw new NotSupportedException();

        public override void Write(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                Release();
                responseIds_.Writer.TryComplete();
                currentFrame_.Dispose();
            }

            base.Dispose(disposing);
        }
    }
}

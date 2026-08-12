using System;
using System.IO;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.CommandLine;
using Xunit;

namespace Asharia.Studio.Observe.Tests;

public sealed class StudioObserveCommandTests
{
    [Fact]
    public void Parser_accepts_only_the_current_exact_command_surface()
    {
        var instanceId = Guid.NewGuid();

        var list = StudioObserveCommand.Parse(["list", "--format", "json"]);
        var describe = StudioObserveCommand.Parse(
            [
                "describe",
                "--instance",
                instanceId.ToString("D"),
                "--timeout-ms",
                "25",
            ]);
        var diagnostics = StudioObserveCommand.Parse(
            [
                "diagnostics",
                "--instance",
                instanceId.ToString("D"),
                "--after",
                "7",
                "--max",
                "25",
                "--channel",
                "problem",
            ]);
        var logs = StudioObserveCommand.Parse(
            ["logs", "--instance", instanceId.ToString("D"), "--max", "5"]);
        var uiWindows = StudioObserveCommand.Parse(
            ["ui-list-windows", "--instance", instanceId.ToString("D")]);
        var uiTree = StudioObserveCommand.Parse(
            [
                "ui-read-tree",
                "--instance",
                instanceId.ToString("D"),
                "--window",
                "StudioShellWindow",
                "--max-depth",
                "4",
                "--max",
                "25",
            ]);

        Assert.Equal(StudioObserveVerb.List, list.Invocation!.Verb);
        Assert.Equal(StudioObserveOutputFormat.Json, list.Invocation.Format);
        Assert.Equal(StudioObserveVerb.Describe, describe.Invocation!.Verb);
        Assert.Equal(new StudioInstanceId(instanceId), describe.Invocation.StudioInstanceId);
        Assert.Equal(25, describe.Invocation.TimeoutMilliseconds);
        Assert.Equal(StudioObserveVerb.Diagnostics, diagnostics.Invocation!.Verb);
        Assert.Equal(7, diagnostics.Invocation.AfterSequence);
        Assert.Equal(25, diagnostics.Invocation.MaxCount);
        Assert.Equal("problem", diagnostics.Invocation.DiagnosticChannel);
        Assert.Equal(StudioObserveVerb.Logs, logs.Invocation!.Verb);
        Assert.Equal(5, logs.Invocation.MaxCount);
        Assert.Equal(StudioObserveVerb.UiListWindows, uiWindows.Invocation!.Verb);
        Assert.Equal(StudioObserveVerb.UiReadTree, uiTree.Invocation!.Verb);
        Assert.Equal("StudioShellWindow", uiTree.Invocation.WindowId);
        Assert.Equal(4, uiTree.Invocation.MaxDepth);
        Assert.Equal(25, uiTree.Invocation.MaxCount);
        Assert.Null(list.Error);
        Assert.Null(describe.Error);
        Assert.Null(diagnostics.Error);
        Assert.Null(logs.Error);
        Assert.Null(uiWindows.Error);
        Assert.Null(uiTree.Error);
    }

    [Theory]
    [InlineData()]
    [InlineData("state")]
    [InlineData("ui")]
    [InlineData("logs")]
    [InlineData("describe")]
    [InlineData("list", "--instance", "11111111-2222-3333-4444-555555555555")]
    [InlineData("list", "--format", "JSON")]
    [InlineData("list", "--timeout-ms", "0")]
    [InlineData("list", "--format", "json", "--format", "text")]
    [InlineData("describe", "--instance", "11111111-2222-3333-4444-555555555555", "--max", "1")]
    [InlineData("logs", "--instance", "11111111-2222-3333-4444-555555555555", "--channel", "problem")]
    [InlineData("diagnostics", "--instance", "11111111-2222-3333-4444-555555555555", "--after", "-1")]
    [InlineData("diagnostics", "--instance", "11111111-2222-3333-4444-555555555555", "--max", "1001")]
    [InlineData("diagnostics", "--instance", "11111111-2222-3333-4444-555555555555", "--channel", "console")]
    [InlineData("ui-list-windows")]
    [InlineData("ui-list-windows", "--instance", "11111111-2222-3333-4444-555555555555", "--window", "StudioShellWindow")]
    [InlineData("ui-read-tree", "--instance", "11111111-2222-3333-4444-555555555555")]
    [InlineData("ui-read-tree", "--instance", "11111111-2222-3333-4444-555555555555", "--window", "0x1234")]
    [InlineData("ui-read-tree", "--instance", "11111111-2222-3333-4444-555555555555", "--window", "StudioShellWindow", "--max-depth", "17")]
    [InlineData("ui-read-tree", "--instance", "11111111-2222-3333-4444-555555555555", "--window", "StudioShellWindow", "--max", "513")]
    public void Parser_rejects_unimplemented_ambiguous_or_unbounded_commands(
        params string[] arguments)
    {
        var result = StudioObserveCommand.Parse(arguments);

        Assert.Null(result.Invocation);
        Assert.NotNull(result.Error);
    }

    [Fact]
    public async Task Missing_instance_returns_typed_stale_exit()
    {
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            ["describe", "--instance", Guid.NewGuid().ToString("D")],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Stale, exitCode);
        Assert.Contains("observation.discovery.not-found", error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Real_endpoint_lists_and_describes_without_exposing_attach_secret()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifestResult = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath));
        var manifest = manifestResult.Value!;
        using var listOutput = new StringWriter();
        using var listError = new StringWriter();

        var listExit = await StudioObserveCommand.RunAsync(
            ["list", "--format", "json"],
            listOutput,
            listError,
            CancellationToken.None);
        using var describeOutput = new StringWriter();
        using var describeError = new StringWriter();
        var describeExit = await StudioObserveCommand.RunAsync(
            [
                "describe",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--format",
                "json",
            ],
            describeOutput,
            describeError,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Success, listExit);
        Assert.Equal((int)StudioObserveExitCode.Success, describeExit);
        Assert.Contains(
            host.StudioInstanceId.Value.ToString("D"),
            listOutput.ToString(),
            StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, listOutput.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, describeOutput.ToString(), StringComparison.Ordinal);
        var response = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(
            System.Text.Encoding.UTF8.GetBytes(describeOutput.ToString().Trim()));
        Assert.True(response.Succeeded);
        Assert.Equal(ObservationOutcome.Complete, response.Value!.Outcome);
        Assert.Equal(host.StudioSessionId, response.Value.Value!.StudioSessionId);
        Assert.Equal(string.Empty, listError.ToString());
        Assert.Equal(string.Empty, describeError.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Real_cursor_commands_preserve_typed_partial_drop_and_channel_results()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        PublishDiagnostic(hub, "studio.test.debug", StudioDiagnosticChannel.Debug);
        PublishDiagnostic(hub, "studio.test.problem", StudioDiagnosticChannel.Problem);
        PublishLog(hub, "Log 1.");
        PublishLog(hub, "Log 2.");
        PublishLog(hub, "Log 3.");
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var diagnosticsOutput = new StringWriter();
        using var diagnosticsError = new StringWriter();

        var diagnosticsExit = await StudioObserveCommand.RunAsync(
            [
                "diagnostics",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--channel",
                "problem",
                "--max",
                "1",
                "--format",
                "json",
            ],
            diagnosticsOutput,
            diagnosticsError,
            CancellationToken.None);
        using var logsOutput = new StringWriter();
        using var logsError = new StringWriter();
        var logsExit = await StudioObserveCommand.RunAsync(
            [
                "logs",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--after",
                "0",
                "--max",
                "1",
            ],
            logsOutput,
            logsError,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Success, diagnosticsExit);
        var diagnostics = ObservationProtocolJson.ReadResponse<
            ObservationCursorWindow<ObservationDiagnosticEvent>>(
            System.Text.Encoding.UTF8.GetBytes(diagnosticsOutput.ToString().Trim()));
        Assert.True(diagnostics.Succeeded);
        Assert.Equal("studio.test.problem", Assert.Single(
            diagnostics.Value!.Value!.Items).Code);
        Assert.Equal((int)StudioObserveExitCode.Partial, logsExit);
        Assert.Contains("Log 2.", logsOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("dropped=1", logsOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("expired=true", logsOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("truncated=true", logsOutput.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, diagnosticsError.ToString());
        Assert.Equal(string.Empty, logsError.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Cursor_command_rejects_a_response_that_moves_behind_the_request()
    {
        var hub = new RegressingCursorHub();
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            [
                "logs",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--after",
                "100",
                "--max",
                "1",
            ],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Protocol, exitCode);
        Assert.Contains("observation.client.invalid-cursor", error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Theory]
    [InlineData("diagnostics")]
    [InlineData("logs")]
    [SupportedOSPlatform("windows")]
    public async Task Cursor_commands_reject_contradictory_retention_evidence(
        string command)
    {
        var hub = new ContradictoryCursorHub();
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            [
                command,
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--after",
                "0",
                "--max",
                "1",
            ],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Protocol, exitCode);
        Assert.Contains(
            "observation.client.invalid-cursor",
            error.ToString(),
            StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Real_endpoint_ui_commands_preserve_typed_semantics_bounds_and_secrets()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub, new FixedUiObservationSource());
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifest = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath)).Value!;
        using var windowsOutput = new StringWriter();
        using var windowsError = new StringWriter();

        var windowsExit = await StudioObserveCommand.RunAsync(
            [
                "ui-list-windows",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--format",
                "json",
            ],
            windowsOutput,
            windowsError,
            CancellationToken.None);
        using var treeOutput = new StringWriter();
        using var treeError = new StringWriter();
        var treeExit = await StudioObserveCommand.RunAsync(
            [
                "ui-read-tree",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--window",
                "StudioShellWindow",
                "--max",
                "1",
            ],
            treeOutput,
            treeError,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Success, windowsExit);
        var windows = ObservationProtocolJson.ReadResponse<UiWindowListResult>(
            System.Text.Encoding.UTF8.GetBytes(windowsOutput.ToString().Trim()));
        Assert.True(windows.Succeeded);
        Assert.Equal("StudioShellWindow", Assert.Single(windows.Value!.Value!.Windows).WindowId);
        Assert.Equal((int)StudioObserveExitCode.Partial, treeExit);
        Assert.Contains("window=StudioShellWindow", treeOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("nodes=1", treeOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("truncated=true", treeOutput.ToString(), StringComparison.Ordinal);
        Assert.Contains("reason=ui.max-nodes", treeOutput.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, windowsOutput.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, treeOutput.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, windowsError.ToString());
        Assert.Equal(string.Empty, treeError.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Ui_command_rejects_a_session_that_does_not_advertise_the_capability()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            [
                "ui-list-windows",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
            ],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Unavailable, exitCode);
        Assert.Contains("observation.capability.unavailable", error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Ui_read_client_timeout_releases_connection_without_stopping_the_host()
    {
        using var source = new BlockingUiObservationSource();
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();
        try
        {
            var command = StudioObserveCommand.RunAsync(
                [
                    "ui-read-tree",
                    "--instance",
                    host.StudioInstanceId.Value.ToString("D"),
                    "--window",
                    "StudioShellWindow",
                    "--timeout-ms",
                    "1000",
                ],
                output,
                error,
                CancellationToken.None);
            Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(2)));

            var exitCode = await command;

            Assert.Equal((int)StudioObserveExitCode.TimedOut, exitCode);
            Assert.Contains("observation.client.timed-out", error.ToString(), StringComparison.Ordinal);
            Assert.Equal(StudioDevelopmentHostState.Running, host.State);
        }
        finally
        {
            source.Release.Set();
        }
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Ui_read_caller_cancellation_is_distinct_from_timeout()
    {
        using var source = new BlockingUiObservationSource();
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub, source);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var cancellation = new CancellationTokenSource();
        using var output = new StringWriter();
        using var error = new StringWriter();
        try
        {
            var command = StudioObserveCommand.RunAsync(
                [
                    "ui-read-tree",
                    "--instance",
                    host.StudioInstanceId.Value.ToString("D"),
                    "--window",
                    "StudioShellWindow",
                    "--timeout-ms",
                    "1000",
                ],
                output,
                error,
                cancellation.Token);
            Assert.True(source.Entered.Wait(TimeSpan.FromSeconds(2)));
            cancellation.Cancel();

            var exitCode = await command;

            Assert.Equal((int)StudioObserveExitCode.Cancelled, exitCode);
            Assert.Contains("observation.client.cancelled", error.ToString(), StringComparison.Ordinal);
        }
        finally
        {
            source.Release.Set();
        }
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Wrong_manifest_token_returns_authorization_without_echoing_secrets()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifest = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath)).Value!;
        var wrongToken = StudioDevelopmentPipeServer.CreateAttachToken();
        await File.WriteAllBytesAsync(
            endpoint.ManifestPath,
            ObservationProtocolJson.WriteSessionManifest(
                manifest with { AttachToken = wrongToken }));
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            [
                "describe",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
            ],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.Authorization, exitCode);
        Assert.Contains("observation.handshake.denied", error.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, error.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(wrongToken, error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Cancelled_discovery_returns_cancelled_exit()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            ["list"],
            output,
            error,
            cancellation.Token);

        Assert.Equal((int)StudioObserveExitCode.Cancelled, exitCode);
        Assert.Contains("observation.client.cancelled", error.ToString(), StringComparison.Ordinal);
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Unavailable_pipe_obeys_client_deadline_and_returns_timeout_exit()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifest = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath)).Value!;
        await File.WriteAllBytesAsync(
            endpoint.ManifestPath,
            ObservationProtocolJson.WriteSessionManifest(manifest with
            {
                PipeName = $"asharia_studio_missing_{Guid.NewGuid():N}",
            }));
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            [
                "describe",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--timeout-ms",
                "25",
            ],
            output,
            error,
            CancellationToken.None);

        Assert.Equal((int)StudioObserveExitCode.TimedOut, exitCode);
        Assert.Contains("observation.client.timed-out", error.ToString(), StringComparison.Ordinal);
        Assert.DoesNotContain(manifest.AttachToken, error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Endpoint_shutdown_revokes_discovery_before_a_new_cli_attach()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var instanceId = host.StudioInstanceId.Value.ToString("D");
        var receipt = await endpoint.StopAsync(TimeSpan.FromSeconds(2));
        using var output = new StringWriter();
        using var error = new StringWriter();

        var exitCode = await StudioObserveCommand.RunAsync(
            ["describe", "--instance", instanceId],
            output,
            error,
            CancellationToken.None);

        Assert.Equal(StudioDevelopmentEndpointStopStatus.Completed, receipt.Status);
        Assert.True(receipt.ManifestRemoved);
        Assert.Equal(StudioDevelopmentPipeServerState.Stopped, endpoint.PipeState);
        Assert.Equal((int)StudioObserveExitCode.Stale, exitCode);
        Assert.Contains("observation.discovery.not-found", error.ToString(), StringComparison.Ordinal);
        Assert.Equal(string.Empty, output.ToString());
        await endpoint.DisposeAsync();
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Cursor_read_timeout_is_distinct_from_caller_cancellation()
    {
        using var hub = new BlockingReadHub();
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();
        try
        {
            var command = StudioObserveCommand.RunAsync(
                [
                    "logs",
                    "--instance",
                    host.StudioInstanceId.Value.ToString("D"),
                    "--timeout-ms",
                    "1000",
                ],
                output,
                error,
                CancellationToken.None);
            Assert.True(hub.Entered.Wait(TimeSpan.FromSeconds(2)));

            var exitCode = await command;

            Assert.Equal((int)StudioObserveExitCode.TimedOut, exitCode);
            Assert.Contains("observation.client.timed-out", error.ToString(), StringComparison.Ordinal);
        }
        finally
        {
            hub.Release.Set();
        }
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Cursor_read_caller_cancellation_returns_cancelled_exit()
    {
        using var hub = new BlockingReadHub();
        await using var host = CreateHost(hub);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        using var output = new StringWriter();
        using var error = new StringWriter();
        using var cancellation = new CancellationTokenSource();
        try
        {
            var command = StudioObserveCommand.RunAsync(
                [
                    "logs",
                    "--instance",
                    host.StudioInstanceId.Value.ToString("D"),
                    "--timeout-ms",
                    "1000",
                ],
                output,
                error,
                cancellation.Token);
            Assert.True(hub.Entered.Wait(TimeSpan.FromSeconds(2)));
            cancellation.Cancel();

            var exitCode = await command;

            Assert.Equal((int)StudioObserveExitCode.Cancelled, exitCode);
            Assert.Contains("observation.client.cancelled", error.ToString(), StringComparison.Ordinal);
        }
        finally
        {
            hub.Release.Set();
        }
    }

    private static StudioDevelopmentHost CreateHost(
        IStudioDiagnosticHub hub,
        IStudioUiObservationSource? uiObservationSource = null) =>
        StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioObserveCommandTests).Module.ModuleVersionId:D}",
            "Test",
            endpointGeneration: 17,
            providerGeneration: 2,
            uiObservationSource);

    private static void PublishDiagnostic(
        StudioDiagnosticHub hub,
        string code,
        StudioDiagnosticChannel channel) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            channel,
            code,
            "test",
            Context(hub, "diagnostics"),
            code));

    private static void PublishLog(StudioDiagnosticHub hub, string message) =>
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "cli",
            Context(hub, "logs"),
            "Log {Sequence}.",
            message));

    private static StudioDiagnosticContext Context(
        IStudioDiagnosticHub hub,
        string component) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.tests",
            component,
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private sealed class BlockingReadHub : IStudioDiagnosticHub, IDisposable
    {
        private readonly StudioDiagnosticHub inner_ = new(
            diagnosticCapacity: 2,
            logCapacity: 2);

        public ManualResetEventSlim Entered { get; } = new(initialState: false);

        public ManualResetEventSlim Release { get; } = new(initialState: false);

        public StudioProcessIdentity ProcessIdentity => inner_.ProcessIdentity;

        public int DiagnosticCapacity => inner_.DiagnosticCapacity;

        public int LogCapacity => inner_.LogCapacity;

        public long SubscriberFailureCount => inner_.SubscriberFailureCount;

        public StudioDiagnosticBufferState DiagnosticBufferState =>
            inner_.DiagnosticBufferState;

        public StudioDiagnosticBufferState LogBufferState => inner_.LogBufferState;

        public long DiagnosticSubscriberFailureCount =>
            inner_.DiagnosticSubscriberFailureCount;

        public long LogSubscriberFailureCount => inner_.LogSubscriberFailureCount;

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            inner_.PublishDiagnostic(write);

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            inner_.PublishLog(write);

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            inner_.ReadDiagnostics(afterSequence, maxCount, channel);

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit)
        {
            Entered.Set();
            if (!Release.Wait(TimeSpan.FromSeconds(5)))
            {
                throw new TimeoutException("Test read release was not signalled.");
            }

            return inner_.ReadLogs(afterSequence, maxCount);
        }

        public StudioDiagnosticRecord? GetLatestDiagnostic() =>
            inner_.GetLatestDiagnostic();

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            inner_.ReadActiveProblems();

        public IDisposable SubscribeDiagnostics(Action invalidated) =>
            inner_.SubscribeDiagnostics(invalidated);

        public IDisposable SubscribeLogs(Action invalidated) =>
            inner_.SubscribeLogs(invalidated);

        public void Dispose()
        {
            Entered.Dispose();
            Release.Dispose();
        }
    }

    private sealed class RegressingCursorHub : IStudioDiagnosticHub
    {
        private readonly StudioDiagnosticHub inner_ = new(
            diagnosticCapacity: 2,
            logCapacity: 2);

        public StudioProcessIdentity ProcessIdentity => inner_.ProcessIdentity;

        public int DiagnosticCapacity => inner_.DiagnosticCapacity;

        public int LogCapacity => inner_.LogCapacity;

        public long SubscriberFailureCount => inner_.SubscriberFailureCount;

        public StudioDiagnosticBufferState DiagnosticBufferState =>
            inner_.DiagnosticBufferState;

        public StudioDiagnosticBufferState LogBufferState => inner_.LogBufferState;

        public long DiagnosticSubscriberFailureCount =>
            inner_.DiagnosticSubscriberFailureCount;

        public long LogSubscriberFailureCount => inner_.LogSubscriberFailureCount;

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            inner_.PublishDiagnostic(write);

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            inner_.PublishLog(write);

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            inner_.ReadDiagnostics(afterSequence, maxCount, channel);

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit) =>
            new(
                OldestAvailableSequence: 1,
                NextCursor: 1,
                TotalDropped: 0,
                CursorExpired: false,
                Truncated: false,
                Items: []);

        public StudioDiagnosticRecord? GetLatestDiagnostic() =>
            inner_.GetLatestDiagnostic();

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            inner_.ReadActiveProblems();

        public IDisposable SubscribeDiagnostics(Action invalidated) =>
            inner_.SubscribeDiagnostics(invalidated);

        public IDisposable SubscribeLogs(Action invalidated) =>
            inner_.SubscribeLogs(invalidated);
    }

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

    private sealed class BlockingUiObservationSource : IStudioUiObservationSource, IDisposable
    {
        public ManualResetEventSlim Entered { get; } = new(initialState: false);

        public ManualResetEventSlim Release { get; } = new(initialState: false);

        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ObservationProtocolReadResult<UiWindowListResult>(
                new UiWindowListResult(DateTimeOffset.UtcNow, []),
                Failure: null));

        public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken)
        {
            Entered.Set();
            if (!Release.Wait(TimeSpan.FromSeconds(5)))
            {
                throw new TimeoutException("Test UI read release was not signalled.");
            }

            return ValueTask.FromResult(new ObservationProtocolReadResult<UiTreeReadResult>(
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
                Failure: null));
        }

        public void Dispose()
        {
            Entered.Dispose();
            Release.Dispose();
        }
    }
}

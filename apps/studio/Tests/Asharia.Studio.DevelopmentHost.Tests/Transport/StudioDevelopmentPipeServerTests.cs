using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.DevelopmentHost.Tests.Transport;

[SupportedOSPlatform("windows")]
public sealed class StudioDevelopmentPipeServerTests
{
    private const long EndpointGeneration = 9;

    [Fact]
    public async Task Current_user_pipe_handshake_and_typed_log_read_roundtrip()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "pipe",
            Context(hub),
            "Pipe event {Sequence}.",
            "Pipe event 1."));
        await using var host = CreateHost(hub);
        var token = StudioDevelopmentPipeServer.CreateAttachToken();
        await using var server = await StudioDevelopmentPipeServer.StartAsync(
            host,
            PipeName(),
            token);
        await using var client = await ConnectAndHandshakeAsync(server, host, token);

        Assert.Equal(StudioDevelopmentPipeServer.MaxClients, client.NumberOfServerInstances);
        var request = Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 2));
        await PipeFrameProtocol.WriteAsync(
            client,
            ObservationProtocolJson.WriteRequest(request),
            ObservationProtocolLimits.MaxRequestBytes,
            CancellationToken.None);
        var responseFrame = await PipeFrameProtocol.ReadAsync(
            client,
            ObservationProtocolLimits.MaxResponseBytes,
            CancellationToken.None);
        var response = ObservationProtocolJson.ReadResponse<
            ObservationCursorWindow<ObservationLogEvent>>(responseFrame!);

        Assert.True(response.Succeeded);
        Assert.Equal(ObservationOutcome.Complete, response.Value!.Outcome);
        Assert.Equal("Pipe event 1.", Assert.Single(response.Value.Value!.Items).RenderedMessage);

        var receipt = await server.StopAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(StudioDevelopmentPipeStopStatus.Completed, receipt.Status);
        Assert.Equal(StudioDevelopmentPipeServerState.Stopped, server.State);
    }

    [Fact]
    public async Task Wrong_token_is_denied_without_echoing_the_secret()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var token = StudioDevelopmentPipeServer.CreateAttachToken();
        await using var server = await StudioDevelopmentPipeServer.StartAsync(
            host,
            PipeName(),
            token);
        await using var client = await ConnectAsync(server.PipeName);
        var wrongToken = StudioDevelopmentPipeServer.CreateAttachToken();
        var handshake = Handshake(host, wrongToken);
        await PipeFrameProtocol.WriteAsync(
            client,
            ObservationProtocolJson.WriteHandshakeRequest(handshake),
            ObservationProtocolLimits.MaxRequestBytes,
            CancellationToken.None);

        var responseFrame = await PipeFrameProtocol.ReadAsync(
            client,
            ObservationProtocolLimits.MaxResponseBytes,
            CancellationToken.None);
        var response = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(responseFrame!);

        Assert.True(response.Succeeded);
        Assert.Equal(ObservationOutcome.Failed, response.Value!.Outcome);
        Assert.Equal("observation.handshake.denied", response.Value.Failure!.Code);
        Assert.DoesNotContain(wrongToken, response.Value.Failure.Message, StringComparison.Ordinal);
        Assert.DoesNotContain(token, response.Value.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Oversized_frame_disconnects_only_that_client_and_worker_recovers()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var token = StudioDevelopmentPipeServer.CreateAttachToken();
        await using var server = await StudioDevelopmentPipeServer.StartAsync(
            host,
            PipeName(),
            token);
        await using (var malformedClient = await ConnectAsync(server.PipeName))
        {
            var header = new byte[sizeof(int)];
            BinaryPrimitives.WriteInt32LittleEndian(
                header,
                ObservationProtocolLimits.MaxRequestBytes + 1);
            await malformedClient.WriteAsync(header);
            await malformedClient.FlushAsync();
        }

        await using var recoveredClient = await ConnectAndHandshakeAsync(
            server,
            host,
            token);
        Assert.True(recoveredClient.IsConnected);
        Assert.Equal(StudioDevelopmentPipeServerState.Running, server.State);
    }

    [Fact]
    public async Task Stop_cancels_idle_connections_and_fixed_accept_workers()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var token = StudioDevelopmentPipeServer.CreateAttachToken();
        var server = await StudioDevelopmentPipeServer.StartAsync(host, PipeName(), token);
        await using var client = await ConnectAndHandshakeAsync(server, host, token);

        var first = await server.StopAsync(TimeSpan.FromSeconds(2));
        var second = await server.StopAsync(TimeSpan.FromSeconds(2));

        Assert.Equal(StudioDevelopmentPipeStopStatus.Completed, first.Status);
        Assert.Equal(first, second);
        Assert.Equal(StudioDevelopmentPipeServerState.Stopped, server.State);
        await server.DisposeAsync();
    }

    [Fact]
    public async Task Stop_deadline_times_out_then_finishes_after_blocked_dispatch_drains()
    {
        using var hub = new BlockingReadHub();
        await using var host = CreateHost(hub);
        var token = StudioDevelopmentPipeServer.CreateAttachToken();
        var server = await StudioDevelopmentPipeServer.StartAsync(host, PipeName(), token);
        await using var client = await ConnectAndHandshakeAsync(server, host, token);
        var request = Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 1),
            timeoutMilliseconds: 5_000);
        await PipeFrameProtocol.WriteAsync(
            client,
            ObservationProtocolJson.WriteRequest(request),
            ObservationProtocolLimits.MaxRequestBytes,
            CancellationToken.None);
        Assert.True(hub.Entered.Wait(TimeSpan.FromSeconds(2)));

        var receipt = await server.StopAsync(TimeSpan.FromMilliseconds(25));

        Assert.Equal(StudioDevelopmentPipeStopStatus.TimedOut, receipt.Status);
        Assert.Equal(StudioDevelopmentPipeServerState.Stopping, server.State);
        hub.Release.Set();
        await WaitForStateAsync(server, StudioDevelopmentPipeServerState.Stopped);
    }

    [Fact]
    public async Task Partial_startup_failure_releases_every_created_pipe_instance()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var pipeName = PipeName();
        var pipeOptions = PipeOptions.Asynchronous
            | PipeOptions.CurrentUserOnly
            | PipeOptions.WriteThrough;
        using var blocker = new NamedPipeServerStream(
            pipeName,
            PipeDirection.InOut,
            StudioDevelopmentPipeServer.MaxClients,
            PipeTransmissionMode.Byte,
            pipeOptions,
            4096,
            4096);

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await StudioDevelopmentPipeServer.StartAsync(
                host,
                pipeName,
                StudioDevelopmentPipeServer.CreateAttachToken()));

        blocker.Dispose();
        var replacements = new NamedPipeServerStream[StudioDevelopmentPipeServer.MaxClients];
        try
        {
            for (var index = 0; index < replacements.Length; index++)
            {
                replacements[index] = new NamedPipeServerStream(
                    pipeName,
                    PipeDirection.InOut,
                    StudioDevelopmentPipeServer.MaxClients,
                    PipeTransmissionMode.Byte,
                    pipeOptions,
                    4096,
                    4096);
            }
        }
        finally
        {
            foreach (var replacement in replacements)
            {
                replacement?.Dispose();
            }
        }
    }

    private static StudioDevelopmentHost CreateHost(IStudioDiagnosticHub hub) =>
        StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioDevelopmentPipeServerTests).Module.ModuleVersionId:D}",
            "Test",
            EndpointGeneration,
            providerGeneration: 2);

    private static async Task<NamedPipeClientStream> ConnectAndHandshakeAsync(
        StudioDevelopmentPipeServer server,
        StudioDevelopmentHost host,
        string token)
    {
        var client = await ConnectAsync(server.PipeName);
        try
        {
            var handshake = Handshake(host, token);
            await PipeFrameProtocol.WriteAsync(
                client,
                ObservationProtocolJson.WriteHandshakeRequest(handshake),
                ObservationProtocolLimits.MaxRequestBytes,
                CancellationToken.None);
            var responseFrame = await PipeFrameProtocol.ReadAsync(
                client,
                ObservationProtocolLimits.MaxResponseBytes,
                CancellationToken.None);
            var response = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(responseFrame!);
            Assert.True(response.Succeeded);
            Assert.Equal(ObservationOutcome.Complete, response.Value!.Outcome);
            Assert.Equal(host.StudioSessionId, response.Value.Value!.StudioSessionId);
            return client;
        }
        catch
        {
            await client.DisposeAsync();
            throw;
        }
    }

    private static async Task<NamedPipeClientStream> ConnectAsync(string pipeName)
    {
        var client = new NamedPipeClientStream(
            ".",
            pipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        try
        {
            using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await client.ConnectAsync(cancellation.Token);
            return client;
        }
        catch
        {
            await client.DisposeAsync();
            throw;
        }
    }

    private static ObservationHandshakeRequest Handshake(
        StudioDevelopmentHost host,
        string token) =>
        new(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            token);

    private static ObservationRequest<TParameters> Request<TParameters>(
        StudioDevelopmentHost host,
        ObservationMethodId method,
        TParameters parameters,
        int timeoutMilliseconds = 1_000)
        where TParameters : class =>
        new(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            method,
            timeoutMilliseconds,
            parameters);

    private static string PipeName() =>
        $"asharia_studio_test_{Guid.NewGuid():N}";

    private static StudioDiagnosticContext Context(IStudioDiagnosticHub hub) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.tests",
            "development-pipe",
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private static async Task WaitForStateAsync(
        StudioDevelopmentPipeServer server,
        StudioDevelopmentPipeServerState expected)
    {
        var deadline = Stopwatch.StartNew();
        while (server.State != expected && deadline.Elapsed < TimeSpan.FromSeconds(2))
        {
            await Task.Delay(10);
        }

        Assert.Equal(expected, server.State);
    }

    private sealed class BlockingReadHub : IStudioDiagnosticHub, IDisposable
    {
        private readonly StudioDiagnosticHub inner_ = new(diagnosticCapacity: 2, logCapacity: 2);

        public ManualResetEventSlim Entered { get; } = new(initialState: false);

        public ManualResetEventSlim Release { get; } = new(initialState: false);

        public StudioProcessIdentity ProcessIdentity => inner_.ProcessIdentity;

        public int DiagnosticCapacity => inner_.DiagnosticCapacity;

        public int LogCapacity => inner_.LogCapacity;

        public long SubscriberFailureCount => inner_.SubscriberFailureCount;

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

        public IDisposable Subscribe(Action invalidated) =>
            inner_.Subscribe(invalidated);

        public void Dispose()
        {
            Entered.Dispose();
            Release.Dispose();
        }
    }
}

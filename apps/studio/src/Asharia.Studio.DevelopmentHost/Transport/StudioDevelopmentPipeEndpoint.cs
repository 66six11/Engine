using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.Versioning;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Transport;

public enum StudioDevelopmentEndpointStopStatus
{
    Completed,
    TimedOut,
    Faulted,
}

public sealed record StudioDevelopmentEndpointTeardownReceipt(
    StudioDevelopmentEndpointStopStatus Status,
    DateTimeOffset StartedAtUtc,
    DateTimeOffset CompletedAtUtc,
    bool ManifestRemoved,
    StudioDevelopmentPipeStopStatus PipeStatus,
    string? FailureCode = null);

[SupportedOSPlatform("windows")]
public sealed class StudioDevelopmentPipeEndpoint : IAsyncDisposable
{
    private static readonly TimeSpan DefaultStopTimeout = TimeSpan.FromSeconds(5);

    private readonly CurrentUserManifestStore manifestStore_;
    private readonly StudioDevelopmentPipeServer pipeServer_;
    private readonly object stopGate_ = new();
    private Task<StudioDevelopmentEndpointTeardownReceipt>? stopTask_;

    private StudioDevelopmentPipeEndpoint(
        CurrentUserManifestStore manifestStore,
        StudioDevelopmentPipeServer pipeServer)
    {
        manifestStore_ = manifestStore;
        pipeServer_ = pipeServer;
    }

    public string ManifestPath => manifestStore_.ManifestPath;

    public string PipeName => pipeServer_.PipeName;

    public StudioDevelopmentPipeServerState PipeState => pipeServer_.State;

    public static ValueTask<StudioDevelopmentPipeEndpoint> StartAsync(
        StudioDevelopmentHost host,
        CancellationToken cancellationToken = default) =>
        StartAsync(
            host,
            CurrentUserManifestStore.DefaultRootDirectory(),
            cancellationToken);

    internal static async ValueTask<StudioDevelopmentPipeEndpoint> StartAsync(
        StudioDevelopmentHost host,
        string manifestRootDirectory,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(host);
        cancellationToken.ThrowIfCancellationRequested();
        var descriptorRequest = new ObservationRequest<SessionDescribeParameters>(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            ObservationMethodId.SessionDescribe,
            ObservationProtocolLimits.MaxWaitMilliseconds,
            new SessionDescribeParameters());
        var descriptorResponse = await host.DescribeAsync(
                descriptorRequest,
                cancellationToken)
            .ConfigureAwait(false);
        if (descriptorResponse.Outcome != ObservationOutcome.Complete
            || descriptorResponse.Value is null)
        {
            throw new InvalidOperationException(
                "Running development Host did not provide a session descriptor for endpoint publication.");
        }

        var descriptor = descriptorResponse.Value;
        cancellationToken.ThrowIfCancellationRequested();
        var pipeName = $"asharia_studio_{host.StudioInstanceId.Value:N}_{host.EndpointGeneration}";
        var attachToken = StudioDevelopmentPipeServer.CreateAttachToken();
        var manifestStore = new CurrentUserManifestStore(
            manifestRootDirectory,
            host.StudioInstanceId);
        var pipeServer = await StudioDevelopmentPipeServer.StartAsync(
                host,
                pipeName,
                attachToken,
                cancellationToken)
            .ConfigureAwait(false);
        try
        {
            var createdAtUtc = DateTimeOffset.UtcNow;
            var manifest = new DevelopmentSessionManifest(
                SchemaVersion: 1,
                ObservationProtocolVersion.Current,
                descriptor.StudioInstanceId,
                descriptor.StudioSessionId,
                descriptor.ProcessId,
                descriptor.ProcessStartTimeUtc,
                descriptor.EndpointGeneration,
                pipeName,
                attachToken,
                descriptor.BuildIdentity,
                descriptor.Configuration,
                CapabilityDigest(descriptor),
                createdAtUtc,
                HeartbeatUtc: createdAtUtc);
            await manifestStore.PublishAsync(manifest, cancellationToken)
                .ConfigureAwait(false);
            return new StudioDevelopmentPipeEndpoint(manifestStore, pipeServer);
        }
        catch (Exception publishError)
        {
            Exception? manifestCleanupError = null;
            try
            {
                manifestStore.Remove();
            }
            catch (Exception cleanupError)
            {
                manifestCleanupError = cleanupError;
            }

            var receipt = await pipeServer.StopAsync(DefaultStopTimeout)
                .ConfigureAwait(false);
            if (manifestCleanupError is not null
                || receipt.Status != StudioDevelopmentPipeStopStatus.Completed)
            {
                throw new AggregateException(
                    publishError,
                    manifestCleanupError ?? new InvalidOperationException(
                        "Development Pipe did not stop cleanly after manifest publication failed."));
            }

            throw;
        }
    }

    public ValueTask<StudioDevelopmentEndpointTeardownReceipt> StopAsync(
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        if (timeout < TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        Task<StudioDevelopmentEndpointTeardownReceipt> stopTask;
        lock (stopGate_)
        {
            stopTask_ ??= StopCoreAsync(timeout);
            stopTask = stopTask_;
        }

        return new ValueTask<StudioDevelopmentEndpointTeardownReceipt>(
            stopTask.WaitAsync(cancellationToken));
    }

    public async ValueTask DisposeAsync()
    {
        var receipt = await StopAsync(DefaultStopTimeout).ConfigureAwait(false);
        if (receipt.Status == StudioDevelopmentEndpointStopStatus.TimedOut)
        {
            throw new TimeoutException(
                "Studio development endpoint did not stop within its owner deadline.");
        }

        if (receipt.Status == StudioDevelopmentEndpointStopStatus.Faulted)
        {
            throw new InvalidOperationException(
                $"Studio development endpoint teardown failed: {receipt.FailureCode}.");
        }
    }

    private async Task<StudioDevelopmentEndpointTeardownReceipt> StopCoreAsync(
        TimeSpan timeout)
    {
        var startedAtUtc = DateTimeOffset.UtcNow;
        var startedTimestamp = Stopwatch.GetTimestamp();
        var manifestRemoved = false;
        string? failureCode = null;
        try
        {
            manifestStore_.Remove();
            manifestRemoved = true;
        }
        catch (Exception)
        {
            failureCode = "observation.manifest.remove-failed";
        }

        var pipeReceipt = await pipeServer_.StopAsync(
                Remaining(startedTimestamp, timeout))
            .ConfigureAwait(false);
        var status = pipeReceipt.Status == StudioDevelopmentPipeStopStatus.TimedOut
            ? StudioDevelopmentEndpointStopStatus.TimedOut
            : pipeReceipt.Status == StudioDevelopmentPipeStopStatus.Faulted
                || failureCode is not null
                ? StudioDevelopmentEndpointStopStatus.Faulted
                : StudioDevelopmentEndpointStopStatus.Completed;
        failureCode ??= pipeReceipt.FailureCode;
        return new StudioDevelopmentEndpointTeardownReceipt(
            status,
            startedAtUtc,
            DateTimeOffset.UtcNow,
            manifestRemoved,
            pipeReceipt.Status,
            failureCode);
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

    private static TimeSpan Remaining(long startedTimestamp, TimeSpan timeout)
    {
        if (timeout == Timeout.InfiniteTimeSpan)
        {
            return Timeout.InfiniteTimeSpan;
        }

        var elapsed = Stopwatch.GetElapsedTime(startedTimestamp);
        return elapsed >= timeout ? TimeSpan.Zero : timeout - elapsed;
    }
}

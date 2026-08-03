using System;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.DevelopmentHost.Tests.Transport;

[SupportedOSPlatform("windows")]
public sealed class StudioDevelopmentPipeEndpointTests
{
    private const long EndpointGeneration = 11;

    [Fact]
    public async Task Endpoint_publishes_current_user_manifest_connects_and_removes_before_stop_completes()
    {
        var root = TestRoot();
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        try
        {
            await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(
                host,
                root);

            Assert.True(File.Exists(endpoint.ManifestPath));
            var parsed = ObservationProtocolJson.ReadSessionManifest(
                await File.ReadAllBytesAsync(endpoint.ManifestPath));
            Assert.True(parsed.Succeeded);
            var manifest = parsed.Value!;
            Assert.Equal(host.StudioInstanceId, manifest.StudioInstanceId);
            Assert.Equal(host.StudioSessionId, manifest.StudioSessionId);
            Assert.Equal(host.EndpointGeneration, manifest.EndpointGeneration);
            Assert.Equal(endpoint.PipeName, manifest.PipeName);
            Assert.Equal(44, manifest.AttachToken.Length);
            Assert.Equal(64, manifest.CapabilityDigest.Length);
            AssertProtectedForCurrentUser(new DirectoryInfo(root));
            AssertProtectedForCurrentUser(new FileInfo(endpoint.ManifestPath));

            await using (var client = await ConnectAsync(manifest.PipeName))
            {
                var handshake = new ObservationHandshakeRequest(
                    ObservationProtocolVersion.Current,
                    new ObservationRequestId(Guid.NewGuid()),
                    manifest.StudioInstanceId,
                    manifest.EndpointGeneration,
                    manifest.AttachToken);
                await PipeFrameProtocol.WriteAsync(
                    client,
                    ObservationProtocolJson.WriteHandshakeRequest(handshake),
                    ObservationProtocolLimits.MaxRequestBytes,
                    CancellationToken.None);
                var responseFrame = await PipeFrameProtocol.ReadAsync(
                    client,
                    ObservationProtocolLimits.MaxResponseBytes,
                    CancellationToken.None);
                var response = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(
                    responseFrame!);
                Assert.True(response.Succeeded);
                Assert.Equal(ObservationOutcome.Complete, response.Value!.Outcome);
            }

            var receipt = await endpoint.StopAsync(TimeSpan.FromSeconds(2));

            Assert.Equal(StudioDevelopmentEndpointStopStatus.Completed, receipt.Status);
            Assert.True(receipt.ManifestRemoved);
            Assert.False(File.Exists(endpoint.ManifestPath));
            Assert.Equal(StudioDevelopmentPipeStopStatus.Completed, receipt.PipeStatus);
            Assert.Equal(StudioDevelopmentPipeServerState.Stopped, endpoint.PipeState);
        }
        finally
        {
            SafeDeleteTestRoot(root);
        }
    }

    [Fact]
    public async Task Manifest_publication_failure_removes_discovery_and_stops_pipe()
    {
        var root = TestRoot();
        Directory.CreateDirectory(Path.GetDirectoryName(root)!);
        await File.WriteAllTextAsync(root, "directory creation must fail");
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var expectedPipeName =
            $"asharia_studio_{host.StudioInstanceId.Value:N}_{host.EndpointGeneration}";
        try
        {
            await Assert.ThrowsAnyAsync<Exception>(async () =>
                await StudioDevelopmentPipeEndpoint.StartAsync(host, root));

            Assert.False(File.Exists(Path.Combine(
                root,
                $"{host.StudioInstanceId.Value:D}.json")));
            await Assert.ThrowsAnyAsync<Exception>(async () =>
            {
                await using var client = new NamedPipeClientStream(
                    ".",
                    expectedPipeName,
                    PipeDirection.InOut,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                using var deadline = new CancellationTokenSource(
                    TimeSpan.FromMilliseconds(200));
                await client.ConnectAsync(deadline.Token);
            });
        }
        finally
        {
            if (File.Exists(root))
            {
                File.Delete(root);
            }
        }
    }

    [Fact]
    public async Task Canceled_start_does_not_publish_manifest_or_leave_a_pipe_listener()
    {
        var root = TestRoot();
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        var expectedPipeName =
            $"asharia_studio_{host.StudioInstanceId.Value:N}_{host.EndpointGeneration}";
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
                await StudioDevelopmentPipeEndpoint.StartAsync(
                    host,
                    root,
                    cancellation.Token));

            Assert.False(File.Exists(Path.Combine(
                root,
                $"{host.StudioInstanceId.Value:D}.json")));
            await Assert.ThrowsAnyAsync<Exception>(async () =>
            {
                await using var client = new NamedPipeClientStream(
                    ".",
                    expectedPipeName,
                    PipeDirection.InOut,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                using var deadline = new CancellationTokenSource(
                    TimeSpan.FromMilliseconds(200));
                await client.ConnectAsync(deadline.Token);
            });
        }
        finally
        {
            SafeDeleteTestRoot(root);
        }
    }

    private static StudioDevelopmentHost CreateHost(IStudioDiagnosticHub hub) =>
        StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioDevelopmentPipeEndpointTests).Module.ModuleVersionId:D}",
            "Test",
            EndpointGeneration,
            providerGeneration: 2);

    private static async Task<NamedPipeClientStream> ConnectAsync(string pipeName)
    {
        var client = new NamedPipeClientStream(
            ".",
            pipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        try
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await client.ConnectAsync(deadline.Token);
            return client;
        }
        catch
        {
            await client.DisposeAsync();
            throw;
        }
    }

    private static void AssertProtectedForCurrentUser(FileSystemInfo entry)
    {
        using var identity = WindowsIdentity.GetCurrent();
        var currentUser = identity.User!;
        FileSystemSecurity security = entry switch
        {
            DirectoryInfo directory =>
                FileSystemAclExtensions.GetAccessControl(directory),
            FileInfo file => FileSystemAclExtensions.GetAccessControl(file),
            _ => throw new ArgumentOutOfRangeException(nameof(entry)),
        };
        Assert.True(security.AreAccessRulesProtected);
        Assert.Equal(
            currentUser,
            security.GetOwner(typeof(SecurityIdentifier)));
        var rules = security
            .GetAccessRules(
                includeExplicit: true,
                includeInherited: true,
                typeof(SecurityIdentifier))
            .Cast<FileSystemAccessRule>()
            .ToArray();
        var rule = Assert.Single(rules);
        Assert.False(rule.IsInherited);
        Assert.Equal(AccessControlType.Allow, rule.AccessControlType);
        Assert.Equal(currentUser, rule.IdentityReference);
        Assert.Equal(
            FileSystemRights.FullControl,
            rule.FileSystemRights & FileSystemRights.FullControl);
    }

    private static string TestRoot() => Path.Combine(
        Path.GetTempPath(),
        "asharia-studio-endpoint-tests",
        Guid.NewGuid().ToString("N"));

    private static void SafeDeleteTestRoot(string root)
    {
        if (!Directory.Exists(root))
        {
            return;
        }

        var fullRoot = Path.GetFullPath(root);
        var testParent = Path.GetFullPath(Path.Combine(
            Path.GetTempPath(),
            "asharia-studio-endpoint-tests")) + Path.DirectorySeparatorChar;
        if (!fullRoot.StartsWith(testParent, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Refusing to recursively delete a path outside the endpoint test root.");
        }

        Directory.Delete(fullRoot, recursive: true);
    }
}

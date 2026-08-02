using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.Observe.Tests;

[SupportedOSPlatform("windows")]
public sealed class StudioObserveProcessTests
{
    [Fact]
    public async Task Real_cli_process_attaches_to_real_endpoint_without_visible_window_or_secret_output()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "process",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.tests",
                "observe-process",
                StudioDiagnosticScope.Process(hub.ProcessIdentity)),
            "CLI process event.",
            "CLI process event."));
        await using var host = StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioObserveProcessTests).Module.ModuleVersionId:D}",
            "Test",
            endpointGeneration: 19,
            providerGeneration: 2);
        await using var endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(host);
        var manifest = ObservationProtocolJson.ReadSessionManifest(
            await File.ReadAllBytesAsync(endpoint.ManifestPath)).Value!;
        var assemblyPath = typeof(Asharia.Studio.Observe.Program).Assembly.Location;
        using var process = Process.Start(new ProcessStartInfo
        {
            FileName = "dotnet",
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            ArgumentList =
            {
                assemblyPath,
                "logs",
                "--instance",
                host.StudioInstanceId.Value.ToString("D"),
                "--format",
                "json",
            },
        }) ?? throw new InvalidOperationException("Failed to start observe CLI child process.");
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        try
        {
            var stdoutTask = process.StandardOutput.ReadToEndAsync(deadline.Token);
            var stderrTask = process.StandardError.ReadToEndAsync(deadline.Token);
            await process.WaitForExitAsync(deadline.Token);
            var stdout = await stdoutTask;
            var stderr = await stderrTask;

            Assert.Equal(0, process.ExitCode);
            Assert.Contains(
                "CLI process event.",
                stdout,
                StringComparison.Ordinal);
            Assert.DoesNotContain(manifest.AttachToken, stdout, StringComparison.Ordinal);
            Assert.DoesNotContain(manifest.AttachToken, stderr, StringComparison.Ordinal);
            Assert.Equal(string.Empty, stderr);
        }
        catch
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync();
            }

            throw;
        }
    }
}

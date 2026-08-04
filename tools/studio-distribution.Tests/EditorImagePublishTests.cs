using System.Diagnostics;
using System.Text.Json;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

[Collection(StudioEditorImageInputCollection.Name)]
public sealed class EditorImagePublishTests
{
    private readonly StudioEditorImageTestInputs inputs_;

    public EditorImagePublishTests(StudioEditorImageTestInputs inputs)
    {
        inputs_ = inputs;
    }

    [Fact]
    public async Task Editor_host_profile_cli_stages_the_production_policy()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        var repositoryRoot = FindRepositoryRoot();
        var dotnetHost = FindDotnetHost();
        var temporaryRoot = Path.Combine(
            Path.GetTempPath(),
            $"asharia-editor-profile-cli-test-{Guid.NewGuid():N}");
        Directory.CreateDirectory(temporaryRoot);
        var outputRoot = Path.Combine(temporaryRoot, "editor-profile");

        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = dotnetHost,
                    WorkingDirectory = repositoryRoot,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                },
            };
            foreach (var argument in new[]
            {
                DistributionCliPath(repositoryRoot),
                "stage-editor-host-profile",
                "--output-root",
                outputRoot,
            })
            {
                process.StartInfo.ArgumentList.Add(argument);
            }

            Assert.True(process.Start(), "Could not start the Studio Distribution CLI.");
            var stdout = process.StandardOutput.ReadToEndAsync();
            var stderr = process.StandardError.ReadToEndAsync();
            await WaitForExitOrKillAsync(process, TimeSpan.FromSeconds(30));
            var receiptJson = await stdout;
            var error = await stderr;

            Assert.True(
                process.ExitCode == 0,
                $"Studio Distribution CLI failed with exit code {process.ExitCode}:{Environment.NewLine}{error}");
            Assert.Equal(string.Empty, error);
            using var receipt = JsonDocument.Parse(receiptJson);
            Assert.Equal(
                Path.GetFullPath(outputRoot),
                receipt.RootElement.GetProperty("root").GetString());
            Assert.Equal(
                "profiles/editor/asharia.host-profile.json",
                receipt.RootElement.GetProperty("profile").GetProperty("path").GetString());
            Assert.Equal(
                "editor",
                receipt.RootElement.GetProperty("profile").GetProperty("hostKind").GetString());

            var output = File.ReadAllBytes(Path.Combine(
                outputRoot,
                "profiles",
                "editor",
                "asharia.host-profile.json"));
            var source = File.ReadAllBytes(Path.Combine(
                repositoryRoot,
                "apps",
                "studio",
                "Distribution",
                "profiles",
                "editor",
                "asharia.host-profile.json"));
            Assert.Equal(source, output);
        }
        finally
        {
            if (Directory.Exists(temporaryRoot))
            {
                Directory.Delete(temporaryRoot, recursive: true);
            }
        }
    }

    private static async Task<string> QuerySdkVersionAsync(
        string dotnetHost,
        string workingDirectory)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = dotnetHost,
                WorkingDirectory = workingDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        process.StartInfo.ArgumentList.Add("--version");
        Assert.True(process.Start(), "Could not query the pinned .NET SDK.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        await WaitForExitOrKillAsync(process, TimeSpan.FromSeconds(30));
        var error = await stderr;
        Assert.True(
            process.ExitCode == 0,
            $"dotnet --version failed with exit code {process.ExitCode}: {error}");
        return (await stdout).Trim();
    }

    [Fact]
    public async Task Cli_rejects_invalid_path_arguments_without_an_unhandled_exception()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        var repositoryRoot = FindRepositoryRoot();
        var dotnetHost = FindDotnetHost();
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = dotnetHost,
                WorkingDirectory = repositoryRoot,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        foreach (var argument in new[]
        {
            DistributionCliPath(repositoryRoot),
            "stage-editor-image",
            "--publish-root",
            string.Empty,
            "--entry-point",
            "Editor.exe",
            "--dotnet-root",
            "x",
            "--sdk-version",
            inputs_.SdkVersion,
            "--hostfxr-version",
            inputs_.HostFxrVersion,
            "--host-runtime-version",
            inputs_.HostRuntimeVersion,
            "--output-root",
            "x",
        })
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        Assert.True(process.Start(), "Could not start the Studio Distribution CLI.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        await WaitForExitOrKillAsync(process, TimeSpan.FromSeconds(30));

        Assert.Equal(2, process.ExitCode);
        Assert.Equal(string.Empty, await stdout);
        Assert.Contains("Invalid path option", await stderr, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Release_profile_publishes_and_stages_the_current_managed_app()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        var repositoryRoot = FindRepositoryRoot();
        var dotnetHost = FindDotnetHost();
        var temporaryRoot = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-publish-test-{Guid.NewGuid():N}");
        var publishRoot = Path.Combine(temporaryRoot, "publish");

        try
        {
            Assert.Equal(
                inputs_.SdkVersion,
                await QuerySdkVersionAsync(dotnetHost, repositoryRoot));
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = dotnetHost,
                    WorkingDirectory = repositoryRoot,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                },
            };
            process.StartInfo.ArgumentList.Add("publish");
            process.StartInfo.ArgumentList.Add("--disable-build-servers");
            process.StartInfo.ArgumentList.Add(Path.Combine(
                repositoryRoot,
                "apps",
                "studio",
                "Editor.csproj"));
            process.StartInfo.ArgumentList.Add("-c");
            process.StartInfo.ArgumentList.Add("Release");
            process.StartInfo.ArgumentList.Add("-p:PublishProfile=EditorImage");
            process.StartInfo.ArgumentList.Add($"-p:PublishDir={publishRoot}{Path.DirectorySeparatorChar}");

            Assert.True(process.Start(), "Could not start dotnet publish.");
            var stdout = process.StandardOutput.ReadToEndAsync();
            var stderr = process.StandardError.ReadToEndAsync();
            await WaitForExitOrKillAsync(process, TimeSpan.FromMinutes(2));

            var output = (await stdout) + Environment.NewLine + (await stderr);

            Assert.True(
                process.ExitCode == 0,
                $"dotnet publish failed with exit code {process.ExitCode}:{Environment.NewLine}{output}");
            Assert.All(
                new[]
                {
                    "Editor.exe",
                    "Editor.dll",
                    "Editor.deps.json",
                    "Editor.runtimeconfig.json",
                    "Asharia.Studio.Application.dll",
                    "Asharia.Runtime.Contracts.dll",
                    "Asharia.Studio.EngineBridge.dll",
                    "Asharia.Studio.Presentation.Avalonia.dll",
                    "asharia_project_native.dll",
                    "asharia_scene_native.dll",
                    "editor_native.dll",
                },
                file => Assert.True(
                    File.Exists(Path.Combine(publishRoot, file)),
                    $"Published Editor Image input is missing '{file}'.{Environment.NewLine}{output}"));
            Assert.All(
                new[]
                {
                    "Asharia.Editor.dll",
                    "Asharia.Studio.DevelopmentHost.dll",
                    "Asharia.Studio.DevelopmentProtocol.dll",
                    "slang.dll",
                },
                file => Assert.False(
                    File.Exists(Path.Combine(publishRoot, file)),
                    $"Published Editor Image input contains retired artifact '{file}'.{Environment.NewLine}{output}"));

            var dotnetRoot = Path.Combine(temporaryRoot, "dotnet");
            inputs_.CopyDotnetTo(dotnetRoot);
            var editorImageRoot = Path.Combine(temporaryRoot, "editor-image");
            using var cli = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = dotnetHost,
                    WorkingDirectory = repositoryRoot,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                },
            };
            foreach (var argument in new[]
            {
                DistributionCliPath(repositoryRoot),
                "stage-editor-image",
                "--publish-root",
                publishRoot,
                "--entry-point",
                "Editor.exe",
                "--dotnet-root",
                dotnetRoot,
                "--sdk-version",
                inputs_.SdkVersion,
                "--hostfxr-version",
                inputs_.HostFxrVersion,
                "--host-runtime-version",
                inputs_.HostRuntimeVersion,
                "--output-root",
                editorImageRoot,
            })
            {
                cli.StartInfo.ArgumentList.Add(argument);
            }

            Assert.True(cli.Start(), "Could not start the Studio Distribution CLI.");
            var cliStdout = cli.StandardOutput.ReadToEndAsync();
            var cliStderr = cli.StandardError.ReadToEndAsync();
            await WaitForExitOrKillAsync(cli, TimeSpan.FromSeconds(30));
            var receiptJson = await cliStdout;
            var cliError = await cliStderr;
            Assert.True(
                cli.ExitCode == 0,
                $"Studio Distribution CLI failed with exit code {cli.ExitCode}:{Environment.NewLine}{cliError}");
            Assert.Equal(string.Empty, cliError);
            using var receipt = JsonDocument.Parse(receiptJson);
            Assert.Equal(Path.GetFullPath(editorImageRoot), receipt.RootElement.GetProperty("root").GetString());
            Assert.Equal("bin/Editor.exe", receipt.RootElement.GetProperty("entryPoint").GetString());
            var files = receipt.RootElement.GetProperty("files")
                .EnumerateArray()
                .Select(file => file.GetProperty("path").GetString()!)
                .ToArray();
            Assert.Contains(
                $"managed/dotnet/host/fxr/{inputs_.HostFxrVersion}/hostfxr.dll",
                files);
            Assert.Contains(
                $"managed/dotnet/shared/Microsoft.NETCore.App/{inputs_.HostRuntimeVersion}/coreclr.dll",
                files);
            Assert.DoesNotContain("managed/dotnet/dotnet.exe", files);
            Assert.DoesNotContain(
                files,
                path => path.StartsWith("managed/dotnet/sdk/", StringComparison.Ordinal)
                    || path.StartsWith("managed/dotnet/packs/", StringComparison.Ordinal));
            Assert.False(File.Exists(Path.Combine(
                editorImageRoot,
                "managed",
                "dotnet",
                "dotnet.exe")));
            Assert.False(Directory.Exists(Path.Combine(
                editorImageRoot,
                "managed",
                "dotnet",
                "sdk")));
            Assert.False(Directory.Exists(Path.Combine(
                editorImageRoot,
                "managed",
                "dotnet",
                "packs")));
            Assert.False(receipt.RootElement.TryGetProperty("Root", out _));
        }
        finally
        {
            if (Directory.Exists(temporaryRoot))
            {
                Directory.Delete(temporaryRoot, recursive: true);
            }
        }
    }

    private static async Task WaitForExitOrKillAsync(
        Process process,
        TimeSpan timeoutValue)
    {
        using var timeout = new CancellationTokenSource(timeoutValue);
        try
        {
            await process.WaitForExitAsync(timeout.Token);
            return;
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        {
            // Continue into the bounded kill path.
        }

        if (!HasExited(process))
        {
            try
            {
                process.Kill(entireProcessTree: true);
            }
            catch (InvalidOperationException)
            {
                throw new TimeoutException($"Process did not exit within {timeoutValue}.");
            }

            using var killTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await process.WaitForExitAsync(killTimeout.Token);
            }
            catch (InvalidOperationException)
            {
                // The process exited between Kill and the bounded wait.
            }
            catch (OperationCanceledException) when (killTimeout.IsCancellationRequested)
            {
                throw new TimeoutException(
                    "Process did not exit within 5 seconds after it was killed.");
            }
        }

        throw new TimeoutException($"Process did not exit within {timeoutValue}.");
    }

    private static bool HasExited(Process process)
    {
        try
        {
            return process.HasExited;
        }
        catch (InvalidOperationException)
        {
            return true;
        }
    }

    private static string DistributionCliPath(string repositoryRoot) => Path.Combine(
        repositoryRoot,
        "tools",
        "studio-distribution",
        "bin",
        "Release",
        "net10.0",
        "Asharia.Studio.Distribution.dll");

    private static string FindDotnetHost()
    {
        var explicitHost = Environment.GetEnvironmentVariable("DOTNET_HOST_PATH");
        if (!string.IsNullOrWhiteSpace(explicitHost) && File.Exists(explicitHost))
        {
            return explicitHost;
        }

        var runtimeDirectory = new FileInfo(typeof(object).Assembly.Location).Directory!;
        var dotnetRoot = runtimeDirectory.Parent!.Parent!.Parent!;
        var candidate = Path.Combine(dotnetRoot.FullName, "dotnet.exe");
        if (!File.Exists(candidate))
        {
            throw new FileNotFoundException("Could not locate the active dotnet host.", candidate);
        }

        return candidate;
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt"))
                && File.Exists(Path.Combine(
                    current.FullName,
                    "apps",
                    "studio",
                    "Editor.csproj")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the Asharia repository root.");
    }
}

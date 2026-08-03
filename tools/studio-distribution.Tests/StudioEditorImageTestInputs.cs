using System.Diagnostics;
using System.Security;
using System.Text;
using System.Text.Json;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class StudioEditorImageInputCollection
    : ICollectionFixture<StudioEditorImageTestInputs>
{
    public const string Name = "Studio Editor Image inputs";
}

public sealed class StudioEditorImageTestInputs : IDisposable
{
    internal const string StagedEditorMainMarker = "ASHARIA_STAGED_EDITOR_MAIN_OK";

    public StudioEditorImageTestInputs()
    {
        if (!OperatingSystem.IsWindows())
        {
            Root = string.Empty;
            PublishRoot = string.Empty;
            DotnetRoot = string.Empty;
            SdkVersion = string.Empty;
            HostFxrVersion = string.Empty;
            HostRuntimeVersion = string.Empty;
            return;
        }

        Root = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-identity-inputs-{Guid.NewGuid():N}");
        PublishRoot = Path.Combine(Root, "publish");
        DotnetRoot = Path.Combine(Root, "dotnet");
        Directory.CreateDirectory(Root);

        var dotnetHost = FindDotnetHost();
        var installedDotnetRoot = Path.GetDirectoryName(dotnetHost)!;
        var repositoryRoot = FindRepositoryRoot();
        SdkVersion = QueryPinnedNet10SdkVersion(dotnetHost, repositoryRoot);
        var installedSdkRoot = Path.Combine(installedDotnetRoot, "sdk", SdkVersion);
        HostRuntimeVersion = ReadSdkRuntimeVersion(
            Path.Combine(installedSdkRoot, "dotnet.runtimeconfig.json"));
        HostFxrVersion = HostRuntimeVersion;

        CreateMinimalDotnetInput(installedDotnetRoot, installedSdkRoot);
        CreatePublishedEditorFixture(repositoryRoot, dotnetHost);
    }

    public string Root { get; }

    public string PublishRoot { get; }

    public string DotnetRoot { get; }

    public string SdkVersion { get; }

    public string HostFxrVersion { get; }

    public string HostRuntimeVersion { get; }

    internal void CopyPublishTo(string destination) => CopyTree(PublishRoot, destination);

    internal void CopyDotnetTo(string destination) => CopyTree(DotnetRoot, destination);

    public void Dispose()
    {
        if (!string.IsNullOrEmpty(Root) && Directory.Exists(Root))
        {
            Directory.Delete(Root, recursive: true);
        }
    }

    private void CreatePublishedEditorFixture(
        string repositoryRoot,
        string dotnetHost)
    {
        var projectRoot = Path.Combine(Root, "editor-project");
        Directory.CreateDirectory(projectRoot);
        var globalJson = $$"""
            {
              "sdk": {
                "version": "{{SdkVersion}}",
                "rollForward": "disable",
                "allowPrerelease": false
              }
            }
            """;
        File.WriteAllText(
            Path.Combine(projectRoot, "global.json"),
            globalJson + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        var application = Path.Combine(
            repositoryRoot,
            "apps",
            "studio",
            "src",
            "Asharia.Studio.Application",
            "Asharia.Studio.Application.csproj");
        var project = $$"""
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <OutputType>WinExe</OutputType>
                <TargetFramework>net10.0</TargetFramework>
                <RuntimeIdentifier>win-x64</RuntimeIdentifier>
                <SelfContained>false</SelfContained>
                <UseAppHost>true</UseAppHost>
                <PublishSingleFile>false</PublishSingleFile>
                <AssemblyName>Editor</AssemblyName>
                <AppHostDotNetSearch>AppRelative</AppHostDotNetSearch>
                <AppHostRelativeDotNet>../managed/dotnet</AppHostRelativeDotNet>
              </PropertyGroup>
              <ItemGroup>
                <ProjectReference Include="{{SecurityElement.Escape(application)}}" />
              </ItemGroup>
            </Project>
            """;
        File.WriteAllText(
            Path.Combine(projectRoot, "Editor.csproj"),
            project,
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        File.WriteAllText(
            Path.Combine(projectRoot, "Program.cs"),
            $"System.Console.WriteLine(\"{StagedEditorMainMarker}\");\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        Run(
            dotnetHost,
            projectRoot,
            TimeSpan.FromMinutes(2),
            "publish",
            "--disable-build-servers",
            Path.Combine(projectRoot, "Editor.csproj"),
            "-c",
            "Release",
            "-o",
            PublishRoot);
    }

    private void CreateMinimalDotnetInput(
        string installedDotnetRoot,
        string installedSdkRoot)
    {
        CopyInput(
            Path.Combine(installedSdkRoot, "AppHostTemplate", "apphost.exe"),
            $"sdk/{SdkVersion}/AppHostTemplate/apphost.exe");
        CopyTree(
            Path.Combine(installedDotnetRoot, "host", "fxr", HostFxrVersion),
            Path.Combine(DotnetRoot, "host", "fxr", HostFxrVersion));
        CopyTree(
            Path.Combine(
                installedDotnetRoot,
                "shared",
                "Microsoft.NETCore.App",
                HostRuntimeVersion),
            Path.Combine(
                DotnetRoot,
                "shared",
                "Microsoft.NETCore.App",
                HostRuntimeVersion));
    }

    private void CopyInput(string source, string relativeDestination)
    {
        if (!File.Exists(source))
        {
            throw new FileNotFoundException(
                "Required installed .NET 10 identity input was not found.",
                source);
        }

        var destination = Path.Combine(
            DotnetRoot,
            relativeDestination.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.Copy(source, destination);
    }

    private static void CopyTree(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (var directory in Directory.EnumerateDirectories(
                     source,
                     "*",
                     SearchOption.AllDirectories))
        {
            Directory.CreateDirectory(Path.Combine(destination, Path.GetRelativePath(source, directory)));
        }

        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var target = Path.Combine(destination, Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target);
        }
    }

    private static string ReadSdkRuntimeVersion(string runtimeConfigPath)
    {
        using var document = JsonDocument.Parse(File.ReadAllBytes(runtimeConfigPath));
        return document.RootElement
            .GetProperty("runtimeOptions")
            .GetProperty("framework")
            .GetProperty("version")
            .GetString()
            ?? throw new InvalidDataException("SDK runtimeconfig framework version is missing.");
    }

    private static string QueryPinnedNet10SdkVersion(
        string dotnetHost,
        string repositoryRoot)
    {
        var value = Run(
                dotnetHost,
                repositoryRoot,
                TimeSpan.FromSeconds(30),
                "--version")
            .Trim();
        if (value.Contains('-', StringComparison.Ordinal)
            || !Version.TryParse(value, out var parsed)
            || parsed is not
            {
                Major: 10,
                Minor: 0,
                Build: >= 0,
            })
        {
            throw new InvalidOperationException(
                "Repository global.json must select one stable .NET 10.0 SDK.");
        }

        return value;
    }

    private static string Run(
        string executable,
        string workingDirectory,
        TimeSpan timeout,
        params string[] arguments)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = workingDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        if (!process.Start())
        {
            throw new InvalidOperationException($"Could not start '{executable}'.");
        }

        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(checked((int)timeout.TotalMilliseconds)))
        {
            KillAndWaitWithDeadline(process, TimeSpan.FromSeconds(5), executable);
            throw new TimeoutException($"'{executable}' did not finish within {timeout}.");
        }

        var output = stdout.GetAwaiter().GetResult();
        var error = stderr.GetAwaiter().GetResult();
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"'{executable}' exited with {process.ExitCode}:{Environment.NewLine}{output}{Environment.NewLine}{error}");
        }

        return output;
    }

    private static void KillAndWaitWithDeadline(
        Process process,
        TimeSpan timeout,
        string executable)
    {
        if (HasExited(process))
        {
            return;
        }

        try
        {
            process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException)
        {
            return;
        }

        try
        {
            if (!process.WaitForExit(checked((int)timeout.TotalMilliseconds)))
            {
                throw new TimeoutException(
                    $"'{executable}' did not exit within {timeout} after it was killed.");
            }
        }
        catch (InvalidOperationException)
        {
            // The process exited between Kill and the bounded wait.
        }
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
                && File.Exists(Path.Combine(current.FullName, "apps", "studio", "Editor.csproj")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the Asharia repository root.");
    }

}

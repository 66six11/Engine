using System;
using System.IO;
using System.Linq;
using Editor;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class ProgramSourceTests
{
    [Fact]
    public void Studio_returns_the_classic_desktop_lifetime_exit_code()
    {
        var entryPoint = typeof(App).Assembly.EntryPoint;

        Assert.NotNull(entryPoint);
        Assert.Equal(typeof(int), entryPoint.ReturnType);

        var source = LoadSource("Program.cs");
        Assert.Contains("public static int Main(string[] args)", source, StringComparison.Ordinal);
        Assert.Contains(".StartWithClassicDesktopLifetime(args)", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Studio_uses_platform_detection_without_hardcoded_Windows_rendering_policy()
    {
        var source = LoadSource("Program.cs");

        Assert.Contains(".UsePlatformDetect()", source, StringComparison.Ordinal);
        Assert.Contains(".WithInterFont()", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32PlatformOptions", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32RenderingMode", source, StringComparison.Ordinal);
        Assert.DoesNotContain("OperatingSystem.IsWindows", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Studio_installs_one_owned_framework_log_sink_instead_of_trace()
    {
        var source = LoadSource("Program.cs");

        Assert.Contains("AppBuilder.Configure<App>()", source, StringComparison.Ordinal);
        Assert.DoesNotContain("LogToTrace", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WithDeveloperTools", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Asharia.Studio.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Asharia.Studio.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Asharia.Studio.sln.");
    }
}

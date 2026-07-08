using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class ProgramSourceTests
{
    [Fact]
    public void Studio_prefers_vulkan_rendering_before_windows_fallbacks()
    {
        var source = LoadSource("Program.cs");

        Assert.Contains("Win32PlatformOptions", source, StringComparison.Ordinal);
        Assert.Contains("Win32RenderingMode.Vulkan", source, StringComparison.Ordinal);
        Assert.Contains("Win32RenderingMode.AngleEgl", source, StringComparison.Ordinal);
        Assert.Contains("Win32RenderingMode.Software", source, StringComparison.Ordinal);
        Assert.True(
            source.IndexOf("Win32RenderingMode.Vulkan", StringComparison.Ordinal)
                < source.IndexOf("Win32RenderingMode.AngleEgl", StringComparison.Ordinal),
            "Studio should try Vulkan before falling back to ANGLE.");
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
            && File.Exists(Path.Combine(workspaceRoot, "Editor.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}

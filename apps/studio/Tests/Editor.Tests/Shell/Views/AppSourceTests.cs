using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class AppSourceTests
{
    [Fact]
    public void Desktop_exit_shuts_down_native_viewport_runtime_before_process_teardown()
    {
        var source = LoadSource("App.axaml.cs");

        Assert.Contains("ViewportNativeLibraryApi", source, StringComparison.Ordinal);
        Assert.Contains("ShutdownNativeViewportRuntime();", source, StringComparison.Ordinal);
        Assert.Contains("private static void ShutdownNativeViewportRuntime()", source, StringComparison.Ordinal);
        Assert.Contains("ViewportNativeLibraryApi.Instance.Shutdown();", source, StringComparison.Ordinal);
        Assert.Contains("catch (Exception ex) when (IsNativeBindingException(ex))", source, StringComparison.Ordinal);
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

using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class MainWindowSourceTests
{
    [Fact]
    public void Main_window_pumps_panel_frame_scheduler_while_open()
    {
        var source = LoadSource("Shell", "Views", "Windowing", "MainWindow.axaml.cs");

        Assert.Contains("DispatcherTimer panelFrameTimer_", source, StringComparison.Ordinal);
        Assert.Contains("TimeSpan.FromMilliseconds(16)", source, StringComparison.Ordinal);
        Assert.Contains("panelFrameTimer_.Start()", source, StringComparison.Ordinal);
        Assert.Contains("panelFrameTimer_.Stop()", source, StringComparison.Ordinal);
        Assert.Contains("OnPanelFrameTimerTick", source, StringComparison.Ordinal);
        Assert.Contains("DockWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UtcNow)", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        var fullPathParts = new string[pathParts.Length + 1];
        fullPathParts[0] = root;
        Array.Copy(pathParts, 0, fullPathParts, 1, pathParts.Length);
        return File.ReadAllText(Path.Combine(fullPathParts));
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

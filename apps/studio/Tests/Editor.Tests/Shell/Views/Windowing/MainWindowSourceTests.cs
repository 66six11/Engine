using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class MainWindowSourceTests
{
    [Fact]
    public void Hard_cut_window_is_only_a_retained_view()
    {
        var source = LoadSource("Shell", "Views", "Windowing", "MainWindow.axaml.cs");

        Assert.Contains("InitializeComponent();", source, StringComparison.Ordinal);
        Assert.Contains("OnUnhandledKeyDown", source, StringComparison.Ordinal);
        Assert.Contains("FocusManager?.GetFocusedElement() is TextBox", source, StringComparison.Ordinal);
        Assert.Contains("KeyModifiers.Control | KeyModifiers.Meta", source, StringComparison.Ordinal);
        Assert.Contains("viewModel.UndoSceneCommand", source, StringComparison.Ordinal);
        Assert.Contains("viewModel.RedoSceneCommand", source, StringComparison.Ordinal);
        Assert.DoesNotContain("DispatcherTimer", source, StringComparison.Ordinal);
        Assert.DoesNotContain("P/Invoke", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Native", source, StringComparison.Ordinal);
        Assert.DoesNotContain("OperatingSystem.", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32", source, StringComparison.Ordinal);
        Assert.DoesNotContain("MainWindowViewModel", source, StringComparison.Ordinal);
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

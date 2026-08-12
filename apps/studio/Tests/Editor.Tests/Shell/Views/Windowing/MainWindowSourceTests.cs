using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class MainWindowSourceTests
{
    [Fact]
    public void Windows_delegate_shortcuts_to_the_shared_action_router()
    {
        var source = LoadSource("Shell", "Views", "Windowing", "MainWindow.axaml.cs");
        var floatingSource = LoadSource(
            "Shell",
            "Views",
            "Docking",
            "EditorDockFloatingWindow.axaml.cs");
        var routerSource = LoadSource(
            "Shell",
            "Commands",
            "StudioActionShortcutRouter.cs");

        Assert.Contains("InitializeComponent();", source, StringComparison.Ordinal);
        Assert.Contains("OnUnhandledKeyDown", source, StringComparison.Ordinal);
        Assert.Contains("StudioActionShortcutRouter.TryRoute(", source, StringComparison.Ordinal);
        Assert.Contains("StudioShellPresentationIds.MainWindow", source, StringComparison.Ordinal);
        Assert.Contains("StudioActionShortcutRouter.TryRoute(", floatingSource, StringComparison.Ordinal);
        Assert.Contains("ActionTopLevelId", floatingSource, StringComparison.Ordinal);
        Assert.Contains("ActivePanelId(viewModel.DockWorkspace)", floatingSource,
            StringComparison.Ordinal);
        Assert.Contains("ActionStateChanged += OnActionStateChanged", source,
            StringComparison.Ordinal);
        Assert.Contains("ActionStateChanged -= OnActionStateChanged", source,
            StringComparison.Ordinal);
        Assert.DoesNotContain("ActiveWindow?.Id", floatingSource, StringComparison.Ordinal);
        Assert.Contains("shell.TryExecuteShortcut(", routerSource, StringComparison.Ordinal);
        Assert.Contains("IsTextInputOwner(focusedElement)", routerSource, StringComparison.Ordinal);
        Assert.Contains("OfType<TextBox>()", routerSource, StringComparison.Ordinal);
        Assert.DoesNotContain("UndoSceneCommand", source, StringComparison.Ordinal);
        Assert.DoesNotContain("RedoSceneCommand", source, StringComparison.Ordinal);
        Assert.DoesNotContain("UndoSceneCommand", floatingSource, StringComparison.Ordinal);
        Assert.DoesNotContain("RedoSceneCommand", floatingSource, StringComparison.Ordinal);
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

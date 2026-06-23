using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class MainWindowXamlTests
{
    [Fact]
    public void Status_bar_binds_latest_status_message_with_severity_classes_and_target_command()
    {
        var xaml = LoadMainWindowXaml();

        Assert.Contains("Classes=\"status-message-status\"", xaml);
        Assert.Contains("Content=\"{Binding StatusMessageText}\"", xaml);
        Assert.Contains("Command=\"{Binding OpenStatusMessageTargetCommand}\"", xaml);
        Assert.Contains("IsHitTestVisible=\"{Binding CanOpenStatusMessageTarget}\"", xaml);
        Assert.Contains("IsVisible=\"{Binding HasStatusMessage}\"", xaml);
        Assert.Contains("Classes.debug=\"{Binding IsStatusMessageDebug}\"", xaml);
        Assert.Contains("Classes.success=\"{Binding IsStatusMessageSuccess}\"", xaml);
        Assert.Contains("Classes.warning=\"{Binding IsStatusMessageWarning}\"", xaml);
        Assert.Contains("Classes.error=\"{Binding IsStatusMessageError}\"", xaml);
        Assert.Contains("Classes.info=\"{Binding IsStatusMessageInfo}\"", xaml);
        Assert.Contains("EditorBrushSuccess", xaml);
        Assert.Contains("EditorBrushWarning", xaml);
        Assert.Contains("EditorBrushError", xaml);
        Assert.Contains("EditorBrushInfo", xaml);
        Assert.DoesNotContain("command" + "-feedback-status", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Command" + "Feedback", xaml, StringComparison.Ordinal);
    }

    private static string LoadMainWindowXaml()
    {
        return LoadSource("Shell", "Views", "MainWindow.axaml");
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

        directory = new DirectoryInfo(AppContext.BaseDirectory);
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

using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class StudioActionMenuProjectorSourceTests
{
    [Fact]
    public void Menu_projection_has_one_metadata_truth_and_preserves_frozen_context()
    {
        var source = LoadSource(
            "Shell",
            "Commands",
            "StudioActionMenuProjector.cs");

        Assert.Contains("shell.ActionCatalog", source);
        Assert.Contains("placement.Kind == kind", source);
        Assert.Contains("leaf.Placement.Path", source);
        Assert.Contains("leaf.Placement.Section", source);
        Assert.Contains("leaf.Placement.Order", source);
        Assert.DoesNotContain("TopLevelOrder", source);
        Assert.DoesNotContain("segments[index], \"Panels\"", source);
        Assert.Contains("actionState.State is { IsVisible: false }", source);
        Assert.Contains("state?.PresentationLabel ?? definition.Label", source);
        Assert.Contains("ToggleType = state?.CheckState", source);
        Assert.Contains("IsChecked = state?.IsChecked ?? false", source);
        Assert.Contains("StudioActionInvocationSource.Menu", source);
        Assert.Contains("StudioPresentationId topLevelId", source);
        Assert.Contains("StudioPresentationId? focusedPanelId", source);
        Assert.Contains(": shell.GetActionCommand(definition.Id)", source);
        Assert.Contains("CommandParameter = context", source);
        Assert.Contains("Tag = definition.Id.Value", source);
    }

    [Fact]
    public void Toolbar_controls_name_the_registered_action_ids()
    {
        var xaml = LoadSource(
            "Shell",
            "Views",
            "Windowing",
            "MainWindow.axaml");

        Assert.Contains(
            "Tag=\"{x:Static actions:StudioShellActionIds.CreateEntity}\"",
            xaml);
        Assert.Contains(
            "Tag=\"{x:Static actions:StudioShellActionIds.CreateMeshEntity}\"",
            xaml);
        Assert.Contains(
            "Tag=\"{x:Static actions:StudioShellActionIds.SaveScene}\"",
            xaml);
        Assert.Contains(
            "Tag=\"{x:Static actions:StudioShellActionIds.UndoScene}\"",
            xaml);
        Assert.Contains(
            "Tag=\"{x:Static actions:StudioShellActionIds.RedoScene}\"",
            xaml);
    }

    [Fact]
    public void Exit_is_an_app_lifetime_request_not_a_document_action()
    {
        var source = LoadSource(
            "Shell",
            "Views",
            "Windowing",
            "MainWindow.axaml.cs");

        Assert.Contains("AppendLifetimeExitItem", source);
        Assert.Contains("studio-lifetime.exit", source);
        Assert.Contains("Close();", source);
        Assert.DoesNotContain("StudioShellActionIds.Exit", source);
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
        if (!string.IsNullOrWhiteSpace(workspaceRoot) &&
            File.Exists(Path.Combine(workspaceRoot, "Asharia.Studio.sln")))
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

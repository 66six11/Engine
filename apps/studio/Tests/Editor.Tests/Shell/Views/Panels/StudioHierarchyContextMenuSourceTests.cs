using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views.Panels;

public sealed class StudioHierarchyContextMenuSourceTests
{
    [Fact]
    public void Row_context_freezes_a_stable_target_and_uses_the_action_executor()
    {
        var source = LoadSource(
            "Shell",
            "Views",
            "Panels",
            "StudioHierarchyPanelView.axaml.cs");

        Assert.Contains("TryCreateFrozenTarget(viewModel, row", source);
        Assert.Contains("StudioActionTarget.Scene(", source);
        Assert.Contains("StudioActionTarget.SceneObject(", source);
        Assert.Contains("CaptureActionContext(", source);
        Assert.Contains("StudioActionInvocationSource.ContextMenu", source);
        Assert.Contains("StudioActionMenuProjector.ProjectContextMenu(", source);
        Assert.DoesNotContain("CreateEntityCommand.Execute", source);
        Assert.DoesNotContain("SelectedRow = row", source);
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

using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Features.Hierarchy;

public sealed class HierarchyPanelViewXamlTests
{
    [Fact]
    public void Row_double_tap_toggles_only_the_target_node_expansion()
    {
        var xaml = LoadSource("Features", "Hierarchy", "Views", "HierarchyPanelView.axaml");
        var source = LoadSource("Features", "Hierarchy", "Views", "HierarchyPanelView.axaml.cs");

        Assert.Contains("DoubleTapped=\"OnHierarchyRowDoubleTapped\"", xaml);
        Assert.Contains("private void OnHierarchyRowDoubleTapped(object? sender, TappedEventArgs e)", source);
        Assert.Contains("sender is Control { DataContext: HierarchyNodeRowViewModel { HasChildren: true } row }", source);
        Assert.Contains("row.ToggleExpandedCommand.Execute(row);", source);
        Assert.Contains("e.Handled = true;", source);
    }

    [Fact]
    public void Hierarchy_view_uses_shared_editor_tree_visual_layer()
    {
        var xaml = LoadSource("Features", "Hierarchy", "Views", "HierarchyPanelView.axaml");

        Assert.Contains("xmlns:tree=\"using:Editor.UI.Controls.Tree\"", xaml);
        Assert.DoesNotContain("using:Editor.Features.Hierarchy.Controls", xaml);
        Assert.Contains("<tree:EditorTreeIndentGuide", xaml);
        Assert.Contains("Classes=\"hierarchy-list editor-tree-list\"", xaml);
        Assert.Contains("Classes=\"hierarchy-row editor-tree-row\"", xaml);
        Assert.Contains("Classes=\"hierarchy-expander editor-tree-expander\"", xaml);
        Assert.Contains("Classes=\"hierarchy-node-icon editor-tree-node-icon\"", xaml);
        Assert.Contains("Classes=\"hierarchy-node-title editor-tree-primary-text\"", xaml);
        Assert.Contains("Classes=\"hierarchy-node-kind editor-tree-secondary-text\"", xaml);
        Assert.Contains("Classes=\"hierarchy-empty editor-tree-empty\"", xaml);
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

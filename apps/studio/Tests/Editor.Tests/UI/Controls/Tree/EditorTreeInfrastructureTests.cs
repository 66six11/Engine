using System;
using System.IO;
using Xunit;

namespace Editor.Tests.UI.Controls.Tree;

public sealed class EditorTreeInfrastructureTests
{
    [Fact]
    public void App_registers_editor_tree_styles()
    {
        var app = LoadSource("App.axaml");

        Assert.Contains(
            "StyleInclude Source=\"avares://Editor/UI/Controls/Tree/EditorTreeStyles.axaml\"",
            app);
    }

    [Fact]
    public void Editor_tree_styles_define_shared_editor_tree_visual_classes()
    {
        var stylesPath = GetSourcePath("UI", "Controls", "Tree", "EditorTreeStyles.axaml");

        Assert.True(File.Exists(stylesPath), "Expected shared editor tree styles under UI/Controls/Tree.");

        var styles = File.ReadAllText(stylesPath);
        Assert.Contains("ListBox.editor-tree-list", styles);
        Assert.Contains("Grid.editor-tree-row", styles);
        Assert.Contains("Grid.editor-tree-row:pointerover", styles);
        Assert.Contains("Grid.editor-tree-row.selected", styles);
        Assert.Contains("tree|EditorTreeIndentGuide.editor-tree-indent-guide", styles);
        Assert.Contains("controls|IconButton.editor-tree-expander", styles);
        Assert.Contains("icons|EditorIconView.editor-tree-node-icon", styles);
        Assert.Contains("TextBlock.editor-tree-primary-text", styles);
        Assert.Contains("TextBlock.editor-tree-secondary-text", styles);
        Assert.Contains("Button.editor-tree-label-button", styles);
    }

    [Fact]
    public void Shared_tree_metrics_and_class_names_are_not_feature_specific()
    {
        var metricsPath = GetSourcePath("UI", "Controls", "Tree", "EditorTreeMetrics.cs");
        var classNamesPath = GetSourcePath("UI", "Controls", "Tree", "EditorTreeClassNames.cs");

        Assert.True(File.Exists(metricsPath), "Expected shared editor tree metrics under UI/Controls/Tree.");
        Assert.True(File.Exists(classNamesPath), "Expected shared editor tree class names under UI/Controls/Tree.");

        var metrics = File.ReadAllText(metricsPath);
        var classNames = File.ReadAllText(classNamesPath);
        Assert.Contains("namespace Editor.UI.Controls.Tree", metrics);
        Assert.Contains("public const double IndentUnit = 12d;", metrics);
        Assert.Contains("public const double RowHeight = 20d;", metrics);
        Assert.Contains("public const double ExpanderWidth = 18d;", metrics);
        Assert.Contains("namespace Editor.UI.Controls.Tree", classNames);
        Assert.Contains("public const string Row = \"editor-tree-row\";", classNames);
        Assert.Contains("public const string Expander = \"editor-tree-expander\";", classNames);
    }

    private static string LoadSource(params string[] pathParts)
    {
        return File.ReadAllText(GetSourcePath(pathParts));
    }

    private static string GetSourcePath(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        var fullPathParts = new string[pathParts.Length + 1];
        fullPathParts[0] = root;
        Array.Copy(pathParts, 0, fullPathParts, 1, pathParts.Length);
        return Path.Combine(fullPathParts);
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

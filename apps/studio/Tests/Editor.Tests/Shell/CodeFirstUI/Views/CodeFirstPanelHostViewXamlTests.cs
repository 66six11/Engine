using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.CodeFirstUI.Views;

public sealed class CodeFirstPanelHostViewXamlTests
{
    [Fact]
    public void Navigation_directory_uses_compact_transparent_tree_rows_without_side_guides()
    {
        var xaml = LoadSource("Shell", "CodeFirstUI", "Views", "CodeFirstPanelHostView.axaml");
        var source = LoadSource("Shell", "CodeFirstUI", "Adapters", "GuiAvaloniaControlFactory.cs");

        Assert.DoesNotContain("xmlns:hierarchy", xaml);
        Assert.DoesNotContain("HierarchyIndentGuide", xaml);
        Assert.DoesNotContain("code-first-navigation-indent-guide", xaml);

        var directoryStyle = GetStyle(xaml, "Style Selector=\"ScrollViewer.code-first-navigation-directory\"");
        Assert.Contains("Property=\"Background\" Value=\"Transparent\"", directoryStyle);
        Assert.DoesNotContain("EditorBrushSurface01", directoryStyle);
        Assert.Contains("Property=\"Padding\" Value=\"2\"", directoryStyle);

        Assert.DoesNotContain("Style Selector=\"Grid.code-first-navigation-tree-row:pointerover\"", xaml);
        Assert.DoesNotContain("Style Selector=\"Grid.code-first-navigation-tree-row.selected\"", xaml);
        Assert.DoesNotContain("Property=\"Height\" Value=\"20\"", GetStyle(xaml, "Style Selector=\"Grid.code-first-navigation-tree-row\""));
        Assert.Contains("EditorTreeMetrics.IndentUnit", source);
        Assert.Contains("EditorTreeMetrics.ExpanderWidth", source);
        Assert.Contains("EditorTreeMetrics.IconSize", source);
        Assert.Contains("EditorTreeClassNames.Row", source);
        Assert.Contains("EditorTreeClassNames.Expander", source);
        Assert.Contains("EditorTreeClassNames.PrimaryText", source);
        Assert.Contains("EditorTreeClassNames.LabelButton", source);
    }

    private static string GetStyle(string xaml, string selector)
    {
        var start = xaml.IndexOf(selector, StringComparison.Ordinal);
        if (start < 0)
        {
            return string.Empty;
        }

        var end = xaml.IndexOf("</Style>", start, StringComparison.Ordinal);
        return end < 0 ? xaml[start..] : xaml[start..(end + "</Style>".Length)];
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

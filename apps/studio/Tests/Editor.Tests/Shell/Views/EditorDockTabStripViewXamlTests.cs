using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class EditorDockTabStripViewXamlTests
{
    [Fact]
    public void Overflow_affordances_block_click_through_to_underlying_tabs()
    {
        var xaml = LoadTabStripXaml();

        Assert.Contains(
            "Property=\"IsHitTestVisible\" Value=\"True\"",
            xaml);
        Assert.DoesNotContain(
            "Property=\"IsHitTestVisible\" Value=\"False\"",
            GetOverflowAffordanceStyle(xaml));
    }

    [Fact]
    public void Overflow_affordances_use_solid_accent_color()
    {
        var xaml = LoadTabStripXaml();
        var style = GetOverflowAffordanceStyle(xaml);

        Assert.DoesNotContain("Property=\"Opacity\"", style);
        Assert.Contains("EditorBrushAccent", xaml);
    }

    [Fact]
    public void Tab_strip_exposes_visible_viewport_bounds_for_hit_testing()
    {
        var source = LoadSource("Shell", "Views", "EditorDockTabStripView.axaml.cs");

        Assert.Contains("internal bool TryGetViewportBounds(Visual relativeTo, out Rect bounds)", source);
        Assert.Contains("DockTabStripScrollViewer.TranslatePoint(new Point(0, 0), relativeTo)", source);
        Assert.Contains("new Rect(origin.Value, DockTabStripScrollViewer.Bounds.Size)", source);
    }

    [Fact]
    public void Workspace_uses_visible_tab_strip_viewport_for_tab_well_bounds()
    {
        var source = LoadSource("Shell", "Views", "EditorDockWorkspaceView.axaml.cs");

        Assert.Contains("tabStrip.TryGetViewportBounds(DockRoot, out var viewportBounds)", source);
        Assert.Contains("return viewportBounds;", source);
        Assert.Contains("GetTabContentOriginX(host, tabWellBounds.X)", source);
    }

    private static string LoadTabStripXaml()
    {
        return LoadSource("Shell", "Views", "EditorDockTabStripView.axaml");
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
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
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

    private static string GetOverflowAffordanceStyle(string xaml)
    {
        const string selector = "Style Selector=\"Border.owned-dock-tab-overflow-affordance\"";
        var start = xaml.IndexOf(selector, StringComparison.Ordinal);
        if (start < 0)
        {
            return string.Empty;
        }

        var end = xaml.IndexOf("</Style>", start, StringComparison.Ordinal);
        return end < 0 ? xaml[start..] : xaml[start..(end + "</Style>".Length)];
    }
}

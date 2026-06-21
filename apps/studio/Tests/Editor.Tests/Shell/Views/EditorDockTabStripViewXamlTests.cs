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
        Assert.Contains("Property=\"Width\" Value=\"8\"", style);
    }

    [Fact]
    public void Overflow_affordances_start_smooth_hover_auto_scroll()
    {
        var xaml = LoadTabStripXaml();
        var source = LoadSource("Shell", "Views", "EditorDockTabStripView.axaml.cs");

        Assert.Contains("PointerEntered=\"OnLeftOverflowAffordancePointerEntered\"", xaml);
        Assert.Contains("PointerEntered=\"OnRightOverflowAffordancePointerEntered\"", xaml);
        Assert.Contains("PointerExited=\"OnOverflowAffordancePointerExited\"", xaml);
        Assert.Contains("private readonly DispatcherTimer overflowHoverScrollTimer_", source);
        Assert.Contains("OverflowHoverScrollInterval = TimeSpan.FromMilliseconds(16)", source);
        Assert.Contains("OverflowHoverScrollStep = 9.0", source);
        Assert.Contains("StartOverflowHoverScroll(OverflowHoverScrollDirection.Left)", source);
        Assert.Contains("StartOverflowHoverScroll(OverflowHoverScrollDirection.Right)", source);
        Assert.Contains("step: OverflowHoverScrollStep", source);
        Assert.Contains("StopOverflowHoverScroll()", source);
        Assert.Contains("OnOverflowHoverScrollTimerTick", source);
        Assert.True(CountOccurrences(source, "!CanOverflowHoverScroll(direction)") >= 2);
    }

    [Fact]
    public void Overflow_affordances_do_not_show_direction_icons()
    {
        var xaml = LoadTabStripXaml();

        Assert.DoesNotContain("xmlns:icons=\"using:Editor.Shell.Icons\"", xaml);
        Assert.DoesNotContain("IconKey=\"studio.ui.chevron-left\"", xaml);
        Assert.DoesNotContain("IconKey=\"studio.ui.chevron-right\"", xaml);
        Assert.DoesNotContain("owned-dock-tab-overflow-icon", xaml);
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

    private static int CountOccurrences(string value, string text)
    {
        var count = 0;
        var startIndex = 0;
        while (true)
        {
            var index = value.IndexOf(text, startIndex, StringComparison.Ordinal);
            if (index < 0)
            {
                return count;
            }

            count++;
            startIndex = index + text.Length;
        }
    }
}

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

    private static string LoadTabStripXaml()
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(
            root,
            "Shell",
            "Views",
            "EditorDockTabStripView.axaml"));
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

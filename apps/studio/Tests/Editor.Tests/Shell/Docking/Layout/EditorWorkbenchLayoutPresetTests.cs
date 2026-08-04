using System.Collections.Generic;
using System.Linq;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.Docking.Layout;

public sealed class EditorWorkbenchLayoutPresetTests
{
    [Fact]
    public void Default_layout_contains_the_complete_scene_editing_workbench()
    {
        var snapshot = EditorWorkbenchLayoutPreset.CreateDefault();
        var windows = EnumerateWindows(snapshot.Root).ToArray();

        Assert.Equal("owned-dock-center", snapshot.ActiveWindowId);
        Assert.Equal(
            ["hierarchy", "inspector", "project", "scene-view"],
            windows.SelectMany(window => window.TabIds).Order().ToArray());
        Assert.Equal(4, windows.Select(window => window.WindowId).Distinct().Count());
        Assert.Empty(snapshot.FloatingWindows);
    }

    [Fact]
    public void Compact_layout_preserves_all_panels_while_tab_stacking_left_tools()
    {
        var snapshot = EditorWorkbenchLayoutPreset.CreateCompact();
        var windows = EnumerateWindows(snapshot.Root).ToArray();

        Assert.Equal(
            ["hierarchy", "inspector", "project", "scene-view"],
            windows.SelectMany(window => window.TabIds).Order().ToArray());
        Assert.Equal(3, windows.Select(window => window.WindowId).Distinct().Count());
        Assert.Contains(
            windows,
            window => window.TabIds.SequenceEqual(["hierarchy", "project"]));
    }

    [Fact]
    public void Workspace_uses_the_scene_workbench_preset_by_default()
    {
        var registry = new PanelRegistry();
        RegisterPanel(registry, "hierarchy", EditorDockArea.Left);
        RegisterPanel(registry, "project", EditorDockArea.Left);
        RegisterPanel(registry, "scene-view", EditorDockArea.Center);
        RegisterPanel(registry, "inspector", EditorDockArea.Right);

        using var workspace = new EditorDockWorkspaceViewModel(registry);
        var windows = EnumerateWindows(workspace.CaptureLayoutSnapshot().Root).ToArray();

        Assert.Equal(4, windows.Length);
        Assert.Contains(windows, window => window.TabIds.SequenceEqual(["hierarchy"]));
        Assert.Contains(windows, window => window.TabIds.SequenceEqual(["project"]));
        Assert.Contains(windows, window => window.TabIds.SequenceEqual(["scene-view"]));
        Assert.Contains(windows, window => window.TabIds.SequenceEqual(["inspector"]));
    }

    private static void RegisterPanel(
        IPanelRegistry registry,
        string id,
        EditorDockArea defaultArea)
    {
        registry.Register(new PanelDescriptor(
            id,
            id,
            id == "scene-view" ? PanelKind.Document : PanelKind.Tool,
            defaultArea,
            "Window",
            DockContentCachePolicy.KeepAlive,
            static () => new object()));
    }

    private static IEnumerable<EditorDockLayoutNodeSnapshot> EnumerateWindows(
        EditorDockLayoutNodeSnapshot? node)
    {
        if (node is null)
        {
            yield break;
        }

        if (node.Kind == "Window")
        {
            yield return node;
            yield break;
        }

        foreach (var window in EnumerateWindows(node.First))
        {
            yield return window;
        }

        foreach (var window in EnumerateWindows(node.Second))
        {
            yield return window;
        }
    }
}

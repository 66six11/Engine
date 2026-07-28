using System.Collections.Generic;
using Asharia.Studio.Application.Selection;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.Docking.Layout;

public sealed class EditorWorkbenchLayoutPresetTests
{
    [Fact]
    public void Default_contains_only_production_panels_and_keeps_optional_tools_recoverable()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition(
            new EditorSelectionService());
        var workspace = new EditorDockWorkspaceViewModel(
            composition.PanelRegistry,
            lifecycleEvents: null,
            panelFrameScheduler: null,
            EditorWorkbenchLayoutPreset.CreateDefault);

        Assert.True(workspace.ContainsPanel("hierarchy"));
        Assert.True(workspace.ContainsPanel("project"));
        Assert.True(workspace.ContainsPanel("scene-view"));
        Assert.True(workspace.ContainsPanel("inspector"));
        Assert.False(workspace.ContainsPanel("console"));
        Assert.False(workspace.ContainsPanel("problems"));
        Assert.False(workspace.ContainsPanel("frame-debugger"));
        Assert.False(workspace.ContainsPanel("ui-style"));
        Assert.Equal("owned-dock-center", workspace.ActiveWindow?.Id);

        Assert.True(workspace.CanOpenPanel("console"));
        Assert.True(workspace.CanOpenPanel("problems"));
        Assert.True(workspace.CanOpenPanel("frame-debugger"));
        Assert.True(workspace.CanOpenPanel("ui-style"));
        Assert.True(workspace.OpenPanel("console"));
        Assert.True(workspace.OpenPanel("frame-debugger"));
        Assert.True(workspace.OpenPanel("ui-style"));
        Assert.True(workspace.ContainsPanel("console"));
    }

    [Fact]
    public void Compact_groups_left_tools_and_preserves_scene_view_and_inspector()
    {
        var snapshot = EditorWorkbenchLayoutPreset.CreateCompact();

        Assert.Equal(
            ["hierarchy", "project", "scene-view", "inspector"],
            CaptureTabIds(snapshot.Root));
        Assert.Equal("owned-dock-center", snapshot.ActiveWindowId);
        Assert.Equal(["hierarchy", "project"], snapshot.Root?.First?.TabIds);
    }

    [Fact]
    public void Reset_returns_to_shell_owned_default_instead_of_opening_every_registered_panel()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition(
            new EditorSelectionService());
        var workspace = new EditorDockWorkspaceViewModel(
            composition.PanelRegistry,
            lifecycleEvents: null,
            panelFrameScheduler: null,
            EditorWorkbenchLayoutPreset.CreateDefault);
        Assert.True(workspace.OpenPanel("frame-debugger"));

        workspace.ResetLayout();

        Assert.False(workspace.ContainsPanel("frame-debugger"));
        Assert.True(workspace.ContainsPanel("project"));
        Assert.True(workspace.ContainsPanel("scene-view"));
    }

    private static string[] CaptureTabIds(EditorDockLayoutNodeSnapshot? node)
    {
        if (node is null)
        {
            return [];
        }

        var tabIds = new List<string>();
        CaptureTabIds(node, tabIds);
        return [.. tabIds];
    }

    private static void CaptureTabIds(
        EditorDockLayoutNodeSnapshot node,
        List<string> tabIds)
    {
        tabIds.AddRange(node.TabIds);
        if (node.First is not null)
        {
            CaptureTabIds(node.First, tabIds);
        }

        if (node.Second is not null)
        {
            CaptureTabIds(node.Second, tabIds);
        }
    }
}

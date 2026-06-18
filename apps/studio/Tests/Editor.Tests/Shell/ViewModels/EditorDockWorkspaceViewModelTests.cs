using System;
using System.Linq;
using Avalonia;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class EditorDockWorkspaceViewModelTests
{
    [Fact]
    public void OpenPanel_reuses_keep_alive_content_after_close()
    {
        var contentFactory = new CountingContentFactory();
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            contentFactory.Create);
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var firstTab = workspace.CenterWindow.Tabs[0];
        var firstContent = firstTab.Content;

        workspace.CloseTab(firstTab);
        workspace.OpenPanel("panel");

        var reopenedTab = workspace.CenterWindow.Tabs[0];
        Assert.Same(firstContent, reopenedTab.Content);
        Assert.Equal(1, contentFactory.CreateCount);
    }

    [Fact]
    public void OpenPanel_recreates_recreate_on_open_content_after_close()
    {
        var contentFactory = new CountingContentFactory();
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            contentFactory.Create);
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var firstTab = workspace.CenterWindow.Tabs[0];
        var firstContent = firstTab.Content;

        workspace.CloseTab(firstTab);
        workspace.OpenPanel("panel");

        var reopenedTab = workspace.CenterWindow.Tabs[0];
        Assert.NotSame(firstContent, reopenedTab.Content);
        Assert.Equal(2, contentFactory.CreateCount);
    }

    [Fact]
    public void CompleteDragInto_returns_floating_window_request_for_cross_workspace_float_target()
    {
        var registry = CreateRegistry("panel", DockContentCachePolicy.KeepAlive, () => new object());
        var sourceWorkspace = new EditorDockWorkspaceViewModel(registry);
        var targetWorkspace = new EditorDockWorkspaceViewModel(new PanelRegistry());
        var tab = sourceWorkspace.CenterWindow.Tabs[0];
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float window");

        sourceWorkspace.BeginDrag(tab);
        var request = sourceWorkspace.CompleteDragInto(targetWorkspace, target);

        Assert.NotNull(request);
        Assert.Equal(new Rect(24, 32, 320, 220), request.Bounds);
        Assert.DoesNotContain(tab, sourceWorkspace.CenterWindow.Tabs);
        Assert.True(request.Window.DockWorkspace.HasDockContent());
    }

    [Fact]
    public void Host_focus_state_propagates_to_active_tab_strip_item()
    {
        var workspace = new EditorDockWorkspaceViewModel(
            CreateRegistry("panel", DockContentCachePolicy.KeepAlive, () => new object()));
        var tabStripItem = workspace.CenterWindow.TabStripItems[0];

        Assert.True(tabStripItem.IsSelectedInFocusedWindow);
        Assert.False(tabStripItem.IsSelectedInInactiveWindow);

        workspace.SetHostFocusState(false);

        Assert.False(tabStripItem.IsSelectedInFocusedWindow);
        Assert.True(tabStripItem.IsSelectedInInactiveWindow);
    }

    [Fact]
    public void ActivateTab_moves_active_window_to_tab_owner()
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "left",
            DockContentCachePolicy.KeepAlive,
            () => new object(),
            DockArea.Left));
        registry.Register(CreateDescriptor(
            "center",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var leftTab = workspace.LeftWindow.Tabs.Single();

        workspace.ActivateTab(leftTab);

        Assert.Same(workspace.LeftWindow, workspace.ActiveWindow);
        Assert.True(leftTab.IsActive);
        Assert.True(workspace.LeftWindow.TabStripItems.Single().IsSelectedInFocusedWindow);
        Assert.False(workspace.CenterWindow.TabStripItems.Single().IsSelectedInFocusedWindow);
    }

    [Fact]
    public void RestoreLayoutSnapshot_creates_only_tabs_present_in_snapshot()
    {
        var includedContentFactory = new CountingContentFactory();
        var excludedContentFactory = new CountingContentFactory();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "included",
            DockContentCachePolicy.RecreateOnOpen,
            includedContentFactory.Create));
        registry.Register(CreateDescriptor(
            "excluded",
            DockContentCachePolicy.RecreateOnOpen,
            excludedContentFactory.Create));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        includedContentFactory.Reset();
        excludedContentFactory.Reset();

        var restored = workspace.RestoreLayoutSnapshot(new EditorDockLayoutSnapshot
        {
            Version = 1,
            ActiveWindowId = "restored-window",
            Root = new EditorDockLayoutNodeSnapshot
            {
                Kind = "Window",
                Id = "restored-node",
                WindowId = "restored-window",
                WindowTitle = "Restored",
                WindowArea = DockArea.Center,
                WindowRole = "Test",
                TabIds = ["included"],
                ActiveTabId = "included",
            },
        });

        Assert.True(restored);
        Assert.Equal(1, includedContentFactory.CreateCount);
        Assert.Equal(0, excludedContentFactory.CreateCount);
        var activeWindow = Assert.IsType<EditorDockWindowViewModel>(workspace.ActiveWindow);
        Assert.Single(activeWindow.Tabs);
        Assert.Equal("included", activeWindow.Tabs[0].Id);
    }

    private static PanelRegistry CreateRegistry(
        string id,
        DockContentCachePolicy cachePolicy,
        Func<object> createContent)
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(id, cachePolicy, createContent));
        return registry;
    }

    private static PanelDescriptor CreateDescriptor(
        string id,
        DockContentCachePolicy cachePolicy,
        Func<object> createContent,
        DockArea area = DockArea.Center)
    {
        return new PanelDescriptor(
            id,
            "Panel",
            PanelKind.Tool,
            area,
            "Window/Panels/Panel",
            cachePolicy,
            createContent);
    }

    private sealed class CountingContentFactory
    {
        public int CreateCount { get; private set; }

        public object Create()
        {
            CreateCount++;
            return new object();
        }

        public void Reset()
        {
            CreateCount = 0;
        }
    }
}

using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Shell.Docking;
using Editor.Shell.Selection;
using Editor.Shell.Services;
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
    public void CloseTab_disposes_recreate_on_open_content()
    {
        var disposable = new RecordingDisposable();
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => disposable);
        var workspace = new EditorDockWorkspaceViewModel(registry);

        workspace.CloseTab(workspace.CenterWindow.Tabs[0]);

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void CloseTab_keeps_keep_alive_content_until_workspace_disposal()
    {
        var disposable = new RecordingDisposable();
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => disposable);
        var workspace = new EditorDockWorkspaceViewModel(registry);

        workspace.CloseTab(workspace.CenterWindow.Tabs[0]);

        Assert.False(disposable.IsDisposed);

        workspace.Dispose();

        Assert.True(disposable.IsDisposed);
    }

    [Fact]
    public void ResetLayout_disposes_recreate_on_open_content_before_recreating_tabs()
    {
        var first = new RecordingDisposable();
        var second = new RecordingDisposable();
        var contentFactory = new QueueContentFactory(first, second);
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            contentFactory.Create);
        var workspace = new EditorDockWorkspaceViewModel(registry);

        workspace.ResetLayout();

        Assert.True(first.IsDisposed);
        Assert.False(second.IsDisposed);
        Assert.Same(second, workspace.CenterWindow.Tabs[0].Content);
    }

    [Fact]
    public void Dispose_releases_hierarchy_snapshot_subscription_created_through_panel_instance_manager()
    {
        var provider = new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh")]));
        var registry = CreateRegistry(
            "hierarchy",
            DockContentCachePolicy.KeepAlive,
            () => new HierarchyPanelViewModel(
                new EditorSelectionService(),
                provider,
                new CapturingUiDispatcher(hasAccess: true)));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            workspace.CenterWindow.Tabs[0].Content);

        workspace.Dispose();
        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh")]));

        Assert.Equal(["Cube"], GetNodeNames(hierarchy.Nodes));
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
    public void ActivateTab_deactivates_previous_panel_before_activating_next_panel()
    {
        var events = new List<string>();
        var first = new RecordingPanelLifecycleSink("first", events);
        var second = new RecordingPanelLifecycleSink("second", events);
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "first",
            DockContentCachePolicy.KeepAlive,
            () => first));
        registry.Register(CreateDescriptor(
            "second",
            DockContentCachePolicy.KeepAlive,
            () => second));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var secondTab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "second");
        events.Clear();

        workspace.ActivateTab(secondTab);

        Assert.Equal(
            [
                "first:Deactivated:first:Center:Main",
                "second:Activated:second:Center:Main",
            ],
            events);
    }

    [Fact]
    public void CloseTab_deactivates_and_detaches_active_panel_before_disposal()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content);
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var tab = workspace.CenterWindow.Tabs.Single();
        events.Clear();

        workspace.CloseTab(tab);

        Assert.Equal(
            [
                "content:Deactivated:panel:Center:Main",
                "content:Detached:panel:Center:Main",
                "content:Disposed",
            ],
            events);
    }

    [Fact]
    public void Floating_active_tab_deactivates_main_panel_then_activates_floating_panel()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content);
        var sourceWorkspace = new EditorDockWorkspaceViewModel(registry);
        var targetWorkspace = new EditorDockWorkspaceViewModel(new PanelRegistry());
        var tab = sourceWorkspace.CenterWindow.Tabs.Single();
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float window");
        events.Clear();

        sourceWorkspace.BeginDrag(tab);
        var request = sourceWorkspace.CompleteDragInto(targetWorkspace, target);

        Assert.NotNull(request);
        Assert.Equal(
            [
                "content:Deactivated:panel:Center:Main",
                "content:Activated:panel:Center:Floating",
            ],
            events);
        Assert.DoesNotContain(events, candidate => candidate.Contains(":Detached:", StringComparison.Ordinal));
        Assert.DoesNotContain(events, candidate => candidate.EndsWith(":Disposed", StringComparison.Ordinal));
    }

    [Fact]
    public void Cross_workspace_tab_into_deactivates_source_before_activating_target()
    {
        var events = new List<string>();
        var sourceContent = new RecordingPanelLifecycleSink("source", events);
        var targetContent = new RecordingPanelLifecycleSink("target", events);
        var sourceWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "source-panel",
            DockContentCachePolicy.KeepAlive,
            () => sourceContent));
        var targetWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "target-panel",
            DockContentCachePolicy.KeepAlive,
            () => targetContent));
        var sourceTab = sourceWorkspace.CenterWindow.Tabs.Single();
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            TargetArea: DockArea.Center,
            TargetId: targetWorkspace.CenterWindow.Id,
            PreviewBounds: new Rect(0, 0, 320, 220),
            Label: "Target tab strip");
        events.Clear();

        sourceWorkspace.BeginDrag(sourceTab);
        var request = sourceWorkspace.CompleteDragInto(targetWorkspace, target);

        Assert.Null(request);
        Assert.Equal(
            [
                "source:Deactivated:source-panel:Center:Main",
                "target:Deactivated:target-panel:Center:Main",
                "source:Activated:source-panel:Center:Main",
            ],
            events);
        Assert.DoesNotContain(events, candidate => candidate.Contains(":Detached:", StringComparison.Ordinal));
        Assert.DoesNotContain(events, candidate => candidate.EndsWith(":Disposed", StringComparison.Ordinal));
    }

    [Fact]
    public void RestoreLayoutSnapshot_attaches_panel_with_restored_window_area()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content,
            DockArea.Left);
        var workspace = new EditorDockWorkspaceViewModel(registry);
        events.Clear();

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
                WindowArea = DockArea.Right,
                WindowRole = "Test",
                TabIds = ["panel"],
                ActiveTabId = "panel",
            },
        });

        Assert.True(restored);
        Assert.Contains("content:Attached:panel:Right:Main", events);
        Assert.Contains("content:Activated:panel:Right:Main", events);
        Assert.DoesNotContain("content:Attached:panel:Left:Main", events);
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
        Func<object> createContent,
        DockArea area = DockArea.Center)
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(id, cachePolicy, createContent, area));
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

    private sealed class QueueContentFactory(params object[] contents)
    {
        private int nextIndex_;

        public object Create()
        {
            return contents[nextIndex_++];
        }
    }

    private sealed class RecordingPanelLifecycleSink(
        string name,
        List<string> events) : IEditorPanelLifecycleSink, IDisposable
    {
        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Attached:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Activated:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Deactivated:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            events.Add($"{name}:Detached:{context.PanelId}:{context.DockArea}:{GetHostKind(context)}");
        }

        public void Dispose()
        {
            events.Add($"{name}:Disposed");
        }

        private static string GetHostKind(EditorPanelLifecycleContext context)
        {
            return context.IsFloatingWorkspace ? "Floating" : "Main";
        }
    }

    private sealed class RecordingDisposable : IDisposable
    {
        public int DisposeCount { get; private set; }

        public bool IsDisposed => DisposeCount > 0;

        public void Dispose()
        {
            DisposeCount++;
        }
    }

    private static string[] GetNodeNames(IReadOnlyList<HierarchyNodeModel> nodes)
    {
        var names = new string[nodes.Count];
        for (var index = 0; index < nodes.Count; index++)
        {
            names[index] = nodes[index].DisplayName;
        }

        return names;
    }

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            action();
        }
    }
}

using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Panels;
using Avalonia;
using Avalonia.Layout;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Shell.Docking.DropTargets;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Asharia.Studio.Application.Selection;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Docking;

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
    public void CloseTab_hide_failure_still_removes_and_releases_panel_once()
    {
        var content = new ThrowingWorkspacePanelSink();
        var workspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        var tab = workspace.CenterWindow.Tabs.Single();
        content.ThrowOnHidden = true;

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.CloseTab(tab));

        Assert.Same(content.HiddenFailure, exception);
        Assert.False(workspace.ContainsPanel("panel"));
        Assert.Equal(1, content.DisposeCount);
        Assert.Empty(workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
        Assert.False(workspace.CloseTab(tab));
        Assert.Equal(1, content.DisposeCount);
    }

    [Fact]
    public void Constructor_show_failure_releases_created_panel_content()
    {
        var content = new ThrowingWorkspacePanelSink
        {
            ThrowOnShown = true,
        };

        var exception = Assert.Throws<InvalidOperationException>(
            () => new EditorDockWorkspaceViewModel(CreateRegistry(
                "panel",
                DockContentCachePolicy.RecreateOnOpen,
                () => content)));

        Assert.Same(content.ShownFailure, exception);
        Assert.Equal(1, content.DisposeCount);
    }

    [Fact]
    public void Floating_restore_show_failure_releases_created_panel_content()
    {
        var content = new ThrowingWorkspacePanelSink
        {
            ThrowOnShown = true,
        };
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "main-panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "floating-panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        var mainWorkspace = new EditorDockWorkspaceViewModel(
            registry,
            lifecycleEvents: null,
            panelFrameScheduler: null,
            defaultLayoutFactory: () => CreateSinglePanelLayoutSnapshot(
                "main-panel",
                "main-window"));

        var exception = Assert.Throws<InvalidOperationException>(
            () => mainWorkspace.TryCreateFloatingWorkspace(
                CreateFloatingWindowSnapshot(
                    "floating-panel",
                    "floating-window"),
                out _));

        Assert.Same(content.ShownFailure, exception);
        Assert.Equal(1, content.DisposeCount);
        Assert.True(mainWorkspace.ContainsPanel("main-panel"));
    }

    [Fact]
    public void Floating_restore_attach_failure_releases_earlier_tabs_in_same_window()
    {
        var goodContent = new ThrowingWorkspacePanelSink();
        var failingContent = new ThrowingWorkspacePanelSink
        {
            ThrowOnAttached = true,
        };
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "main-panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "good-panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => goodContent));
        registry.Register(CreateDescriptor(
            "failing-panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => failingContent));
        var mainWorkspace = new EditorDockWorkspaceViewModel(
            registry,
            lifecycleEvents: null,
            panelFrameScheduler: null,
            defaultLayoutFactory: () => CreateSinglePanelLayoutSnapshot(
                "main-panel",
                "main-window"));
        var snapshot = CreateFloatingWindowSnapshot(
            "good-panel",
            "floating-window");
        snapshot.Root!.TabIds = ["good-panel", "failing-panel"];

        var exception = Assert.Throws<InvalidOperationException>(
            () => mainWorkspace.TryCreateFloatingWorkspace(snapshot, out _));

        Assert.Same(failingContent.AttachedFailure, exception);
        Assert.Equal(1, goodContent.DetachedCount);
        Assert.Equal(1, goodContent.DisposeCount);
        Assert.Equal(1, failingContent.DetachedCount);
        Assert.Equal(1, failingContent.DisposeCount);
    }

    [Fact]
    public void ResetLayout_releases_all_panels_before_reporting_lifecycle_failures()
    {
        var first = new ThrowingWorkspacePanelSink();
        var second = new ThrowingWorkspacePanelSink();
        var firstReplacement = new object();
        var secondReplacement = new object();
        var firstFactory = new QueueContentFactory(first, firstReplacement);
        var secondFactory = new QueueContentFactory(second, secondReplacement);
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "first",
            DockContentCachePolicy.RecreateOnOpen,
            firstFactory.Create));
        registry.Register(CreateDescriptor(
            "second",
            DockContentCachePolicy.RecreateOnOpen,
            secondFactory.Create));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        first.ThrowOnHidden = true;
        second.ThrowOnDetached = true;
        second.ThrowOnDispose = true;

        var exception = Assert.Throws<AggregateException>(workspace.ResetLayout);

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(first.HiddenFailure, item),
            item => Assert.Same(second.DetachedFailure, item),
            item => Assert.Same(second.DisposeFailure, item));
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
        Assert.Same(
            firstReplacement,
            workspace.CenterWindow.Tabs.Single(tab => tab.Id == "first").Content);
        Assert.Same(
            secondReplacement,
            workspace.CenterWindow.Tabs.Single(tab => tab.Id == "second").Content);
    }

    [Fact]
    public void Dispose_attempts_all_keep_alive_panels_and_is_idempotent_after_failures()
    {
        var first = new ThrowingWorkspacePanelSink();
        var second = new ThrowingWorkspacePanelSink();
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
        first.ThrowOnDispose = true;
        second.ThrowOnDispose = true;

        var exception = Assert.Throws<AggregateException>(workspace.Dispose);

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(first.DisposeFailure, item),
            item => Assert.Same(second.DisposeFailure, item));
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
        Assert.False(workspace.HasDockContent());

        workspace.Dispose();
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
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
    public void Host_focus_deactivates_and_reactivates_shown_panel()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var workspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var tab = workspace.CenterWindow.Tabs.Single();
        events.Clear();

        workspace.SetHostFocusState(false);
        workspace.SetHostFocusState(false);
        workspace.SetHostFocusState(true);

        Assert.Equal(
            [
                "content:Deactivated:panel:Center:Main",
                "content:Activated:panel:Center:Main",
            ],
            events);
        Assert.True(tab.IsActive);
        Assert.Same(tab, workspace.CenterWindow.ActiveTab);
    }

    [Fact]
    public void Activating_window_while_unfocused_defers_panel_activation_until_focus_returns()
    {
        var events = new List<string>();
        var center = new RecordingPanelLifecycleSink("center", events);
        var left = new RecordingPanelLifecycleSink("left", events);
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "center",
            DockContentCachePolicy.KeepAlive,
            () => center));
        registry.Register(CreateDescriptor(
            "left",
            DockContentCachePolicy.KeepAlive,
            () => left,
            EditorDockArea.Left));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var leftTab = workspace.LeftWindow.Tabs.Single();
        events.Clear();

        workspace.SetHostFocusState(false);
        workspace.ActivateTab(leftTab);

        Assert.Equal(
            ["center:Deactivated:center:Center:Main"],
            events);
        Assert.Same(workspace.LeftWindow, workspace.ActiveWindow);

        workspace.SetHostFocusState(true);

        Assert.Equal(
            [
                "center:Deactivated:center:Center:Main",
                "left:Activated:left:Left:Main",
            ],
            events);
    }

    [Fact]
    public void Floating_host_focus_uses_floating_panel_lifecycle_context()
    {
        var events = new List<string>();
        var content = new RecordingPanelLifecycleSink("content", events);
        var sourceWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var tab = sourceWorkspace.CenterWindow.Tabs.Single();
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float window");
        sourceWorkspace.BeginDrag(tab);
        var request = Assert.IsType<EditorDockFloatingWindowRequest>(
            sourceWorkspace.CompleteDrag(target));
        events.Clear();

        request.Window.DockWorkspace.SetHostFocusState(true);
        request.Window.DockWorkspace.SetHostFocusState(false);

        Assert.Equal(
            [
                "content:Activated:panel:Center:Floating",
                "content:Deactivated:panel:Center:Floating",
            ],
            events);
    }

    [Fact]
    public void Host_focus_change_completes_state_before_reporting_lifecycle_failure()
    {
        var content = new ThrowingWorkspacePanelSink();
        var workspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var tabStripItem = workspace.CenterWindow.TabStripItems.Single();
        content.ThrowOnDeactivated = true;

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.SetHostFocusState(false));

        Assert.Same(content.DeactivatedFailure, exception);
        Assert.False(workspace.IsHostFocused);
        Assert.True(tabStripItem.IsSelectedInInactiveWindow);
        Assert.Empty(workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void ActivateTab_moves_active_window_to_tab_owner()
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "left",
            DockContentCachePolicy.KeepAlive,
            () => new object(),
            EditorDockArea.Left));
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
    public void ActivatePanel_completes_active_window_transition_before_aggregating_callbacks()
    {
        var center = new ThrowingWorkspacePanelSink();
        var left = new ThrowingWorkspacePanelSink();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "center",
            DockContentCachePolicy.KeepAlive,
            () => center));
        registry.Register(CreateDescriptor(
            "left",
            DockContentCachePolicy.KeepAlive,
            () => left,
            EditorDockArea.Left));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        center.ThrowOnDeactivated = true;
        left.ThrowOnActivated = true;

        var exception = Assert.Throws<AggregateException>(
            () => workspace.ActivatePanel("left"));

        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(center.DeactivatedFailure, item),
            item => Assert.Same(left.ActivatedFailure, item));
        Assert.Same(workspace.LeftWindow, workspace.ActiveWindow);
        Assert.Equal("left", workspace.ActiveWindow?.ActiveTab?.Id);
        var frame = Assert.Single(
            workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
        Assert.Equal("left", frame.Panel.PanelId);
    }

    [Fact]
    public void Cross_window_move_completes_rehost_before_reporting_hide_failure()
    {
        var source = new ThrowingWorkspacePanelSink();
        var target = new ThrowingWorkspacePanelSink();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "source",
            DockContentCachePolicy.KeepAlive,
            () => source));
        registry.Register(CreateDescriptor(
            "target",
            DockContentCachePolicy.KeepAlive,
            () => target,
            EditorDockArea.Left));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var sourceTab = workspace.CenterWindow.Tabs.Single();
        workspace.BeginDrag(sourceTab);
        source.ThrowOnHidden = true;
        var dropTarget = new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            TargetArea: EditorDockArea.Left,
            TargetId: workspace.LeftWindow.Id,
            PreviewBounds: default,
            Label: "Left tab strip");

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.CompleteDrag(dropTarget));

        Assert.Same(source.HiddenFailure, exception);
        Assert.DoesNotContain(sourceTab, workspace.CenterWindow.Tabs);
        Assert.Contains(sourceTab, workspace.LeftWindow.Tabs);
        Assert.Same(sourceTab, workspace.LeftWindow.ActiveTab);
        Assert.Same(workspace.LeftWindow, workspace.ActiveWindow);
        Assert.Equal(0, source.DisposeCount);
        var frame = Assert.Single(
            workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
        Assert.Equal("source", frame.Panel.PanelId);
    }

    [Fact]
    public void Float_hide_failure_rolls_tab_back_before_reporting_error()
    {
        var content = new ThrowingWorkspacePanelSink();
        var workspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var tab = workspace.CenterWindow.Tabs.Single();
        workspace.BeginDrag(tab);
        content.ThrowOnHidden = true;
        var dropTarget = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float");

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.CompleteDrag(dropTarget));

        Assert.Same(content.HiddenFailure, exception);
        Assert.Contains(tab, workspace.CenterWindow.Tabs);
        Assert.Same(tab, workspace.CenterWindow.ActiveTab);
        Assert.Same(workspace.CenterWindow, workspace.ActiveWindow);
        Assert.Equal(0, content.DisposeCount);
        var frame = Assert.Single(
            workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
        Assert.Equal("panel", frame.Panel.PanelId);
    }

    [Fact]
    public void Float_show_failure_discards_candidate_and_restores_source_lease()
    {
        var content = new ThrowingWorkspacePanelSink();
        var workspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.RecreateOnOpen,
            () => content));
        var tab = workspace.CenterWindow.Tabs.Single();
        workspace.BeginDrag(tab);
        content.ThrowOnFloatingShown = true;
        var dropTarget = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float");

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.CompleteDrag(dropTarget));

        Assert.Same(content.ShownFailure, exception);
        Assert.Contains(tab, workspace.CenterWindow.Tabs);
        Assert.Same(tab, workspace.CenterWindow.ActiveTab);
        Assert.Same(workspace.CenterWindow, workspace.ActiveWindow);
        var frame = Assert.Single(
            workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch));
        Assert.Equal("panel", frame.Panel.PanelId);
        Assert.False(frame.Panel.IsFloatingWorkspace);
        Assert.Equal(0, content.DisposeCount);

        Assert.True(workspace.CloseTab(tab));
        Assert.Equal(1, content.DisposeCount);
        workspace.Dispose();
        Assert.Equal(1, content.DisposeCount);
    }

    [Fact]
    public void Float_failure_restores_tab_at_original_index()
    {
        var content = new ThrowingWorkspacePanelSink();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "first",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "middle",
            DockContentCachePolicy.KeepAlive,
            () => content));
        registry.Register(CreateDescriptor(
            "last",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var originalOrder = workspace.CenterWindow.Tabs.Select(tab => tab.Id).ToArray();
        var tab = workspace.CenterWindow.Tabs.Single(candidate => candidate.Id == "middle");
        workspace.BeginDrag(tab);
        content.ThrowOnFloatingShown = true;
        var dropTarget = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float");

        var exception = Assert.Throws<InvalidOperationException>(
            () => workspace.CompleteDrag(dropTarget));

        Assert.Same(content.ShownFailure, exception);
        Assert.Equal(
            originalOrder,
            workspace.CenterWindow.Tabs.Select(candidate => candidate.Id));
        Assert.Same(tab, workspace.CenterWindow.ActiveTab);
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
    public void Floating_panel_stays_inactive_until_real_host_focus_arrives()
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
            ["content:Deactivated:panel:Center:Main"],
            events);
        Assert.False(request.Window.DockWorkspace.IsHostFocused);

        request.Window.DockWorkspace.SetHostFocusState(true);

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
    public void Session_child_tab_move_deactivates_target_before_activating_moved_panel()
    {
        var events = new List<string>();
        var sourceContent = new RecordingPanelLifecycleSink("source", events);
        var targetContent = new RecordingPanelLifecycleSink("target", events);
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "source-panel",
            DockContentCachePolicy.KeepAlive,
            () => sourceContent));
        registry.Register(CreateDescriptor(
            "target-panel",
            DockContentCachePolicy.KeepAlive,
            () => targetContent));
        var sourceWorkspace = new EditorDockWorkspaceViewModel(registry);
        var sourceTab = sourceWorkspace.CenterWindow.Tabs.Single(
            tab => tab.Id == "source-panel");
        var targetTab = sourceWorkspace.CenterWindow.Tabs.Single(
            tab => tab.Id == "target-panel");
        var floatTarget = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float window");
        sourceWorkspace.BeginDrag(targetTab);
        var floatingRequest = Assert.IsType<EditorDockFloatingWindowRequest>(
            sourceWorkspace.CompleteDrag(floatTarget));
        var targetWorkspace = floatingRequest.Window.DockWorkspace;
        sourceWorkspace.SetHostFocusState(false);
        targetWorkspace.SetHostFocusState(true);
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            TargetArea: EditorDockArea.Center,
            TargetId: targetWorkspace.ActiveWindow?.Id,
            PreviewBounds: new Rect(0, 0, 320, 220),
            Label: "Target tab strip");
        events.Clear();

        sourceWorkspace.BeginDrag(sourceTab);
        var moveRequest = sourceWorkspace.CompleteDragInto(targetWorkspace, target);

        Assert.Null(moveRequest);
        Assert.Equal(
            [
                "target:Deactivated:target-panel:Center:Floating",
                "source:Activated:source-panel:Center:Floating",
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
            EditorDockArea.Left);
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
                WindowArea = EditorDockArea.Right,
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
    public void Panel_frame_scheduler_tracks_dock_lifecycle()
    {
        var content = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Active());
        var registry = CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content);
        var workspace = new EditorDockWorkspaceViewModel(registry);
        var tab = workspace.CenterWindow.Tabs.Single();

        workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);
        workspace.CloseTab(tab);
        workspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(16));

        var frame = Assert.Single(content.Frames);
        Assert.Equal("panel", frame.Panel.PanelId);
    }

    [Fact]
    public void Foreign_session_drag_is_rejected_without_moving_panel()
    {
        var sourceContent = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Active());
        var sourceWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "source-panel",
            DockContentCachePolicy.KeepAlive,
            () => sourceContent));
        var targetWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "target-panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        var sourceTab = sourceWorkspace.CenterWindow.Tabs.Single();
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            TargetArea: EditorDockArea.Center,
            TargetId: targetWorkspace.CenterWindow.Id,
            PreviewBounds: new Rect(0, 0, 320, 220),
            Label: "Target tab strip");

        sourceWorkspace.BeginDrag(sourceTab);
        var request = sourceWorkspace.CompleteDragInto(targetWorkspace, target);
        sourceWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);
        targetWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);

        Assert.Null(request);
        Assert.Contains(sourceTab, sourceWorkspace.CenterWindow.Tabs);
        Assert.DoesNotContain(sourceTab, targetWorkspace.CenterWindow.Tabs);
        var frame = Assert.Single(sourceContent.Frames);
        Assert.Equal("source-panel", frame.Panel.PanelId);
    }

    [Fact]
    public void Floating_workspace_reuses_source_panel_frame_scheduler()
    {
        var content = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Active());
        var sourceWorkspace = new EditorDockWorkspaceViewModel(CreateRegistry(
            "panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var targetWorkspace = new EditorDockWorkspaceViewModel(new PanelRegistry());
        var sourceTab = sourceWorkspace.CenterWindow.Tabs.Single();
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: new Rect(24, 32, 320, 220),
            Label: "Float window");

        sourceWorkspace.BeginDrag(sourceTab);
        var request = sourceWorkspace.CompleteDragInto(targetWorkspace, target);

        Assert.NotNull(request);
        Assert.Same(sourceWorkspace.PanelFrameScheduler, request.Window.DockWorkspace.PanelFrameScheduler);
        request.Window.DockWorkspace.SetHostFocusState(true);
        sourceWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);
        Assert.Single(content.Frames);
    }

    [Fact]
    public void Restored_floating_panel_uses_session_scheduler_and_waits_for_host_focus()
    {
        var content = new RecordingLifecycleFrameSink();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "main-panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "floating-panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var mainWorkspace = new EditorDockWorkspaceViewModel(registry);
        Assert.True(mainWorkspace.RestoreLayoutSnapshot(
            CreateSinglePanelLayoutSnapshot("main-panel", "main-window")));
        content.Events.Clear();

        Assert.True(mainWorkspace.TryCreateFloatingWorkspace(
            CreateFloatingWindowSnapshot("floating-panel", "floating-window"),
            out var floatingWorkspace));

        Assert.False(floatingWorkspace.IsHostFocused);
        Assert.Same(
            mainWorkspace.PanelFrameScheduler,
            floatingWorkspace.PanelFrameScheduler);
        Assert.Contains("attached:Floating", content.Events);
        Assert.DoesNotContain("activated:Floating", content.Events);
        mainWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);
        Assert.Empty(content.Frames);

        floatingWorkspace.SetHostFocusState(true);
        mainWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UnixEpoch);

        Assert.Contains("activated:Floating", content.Events);
        var frame = Assert.Single(content.Frames);
        Assert.True(frame.Panel.IsFloatingWorkspace);
    }

    [Fact]
    public void Restored_floating_tab_keeps_session_owned_content_when_source_closes()
    {
        var content = new RecordingDisposable();
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "main-panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "floating-panel",
            DockContentCachePolicy.KeepAlive,
            () => content));
        var mainWorkspace = new EditorDockWorkspaceViewModel(registry);
        Assert.True(mainWorkspace.RestoreLayoutSnapshot(
            CreateSinglePanelLayoutSnapshot("main-panel", "main-window")));
        Assert.True(mainWorkspace.TryCreateFloatingWorkspace(
            CreateFloatingWindowSnapshot("floating-panel", "floating-window"),
            out var floatingWorkspace));
        var tab = floatingWorkspace.ActiveWindow?.ActiveTab;
        Assert.NotNull(tab);
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            TargetArea: EditorDockArea.Center,
            TargetId: mainWorkspace.ActiveWindow?.Id,
            PreviewBounds: default,
            Label: "Main");

        floatingWorkspace.BeginDrag(tab);
        floatingWorkspace.CompleteDragInto(mainWorkspace, target);
        floatingWorkspace.Dispose();

        Assert.False(content.IsDisposed);
        Assert.Contains(tab, mainWorkspace.ActiveWindow?.Tabs ?? []);

        Assert.True(mainWorkspace.CloseTab(tab));
        Assert.False(content.IsDisposed);

        mainWorkspace.Dispose();
        Assert.Equal(1, content.DisposeCount);
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
                WindowArea = EditorDockArea.Center,
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

    [Fact]
    public void CompleteDrag_at_splitter_preserves_local_order_and_measured_weights()
    {
        var workspace = new EditorDockWorkspaceViewModel(CreateLayoutRegistry());
        var tab = workspace.CenterWindow.Tabs.Single(candidate => candidate.Id == "center-secondary");
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.SplitBetween,
            EditorDockDropGuideKind.Insert,
            TargetArea: null,
            TargetId: "split-center-bottom",
            PreviewBounds: default,
            Label: "Center-bottom splitter",
            SplitterFirstExtent: 600,
            SplitterSecondExtent: 300);

        workspace.BeginDrag(tab);
        workspace.CompleteDrag(target);

        var snapshot = workspace.CaptureLayoutSnapshot();
        var targetSplit = FindSnapshotById(snapshot.Root, "split-center-bottom");
        var insertedGroup = Assert.IsType<EditorDockLayoutNodeSnapshot>(targetSplit.Second);

        Assert.Equal(Orientation.Vertical, targetSplit.Orientation);
        Assert.Equal(300, targetSplit.FirstLength?.Value);
        Assert.Equal(600, targetSplit.SecondLength?.Value);
        Assert.Equal(["center-primary", "center-tertiary"], targetSplit.First?.TabIds);
        Assert.Equal(["center-secondary"], insertedGroup.First?.TabIds);
        Assert.Equal(["bottom"], insertedGroup.Second?.TabIds);
        Assert.Equal(450, insertedGroup.FirstLength?.Value);
        Assert.Equal(150, insertedGroup.SecondLength?.Value);
    }

    [Fact]
    public void CompleteDrag_at_workspace_edge_wraps_entire_layout()
    {
        var workspace = new EditorDockWorkspaceViewModel(CreateLayoutRegistry());
        var tab = workspace.CenterWindow.Tabs.Single(candidate => candidate.Id == "center-secondary");
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.InsertWorkspaceLeft,
            EditorDockDropGuideKind.Insert,
            TargetArea: null,
            TargetId: null,
            PreviewBounds: default,
            Label: "Workspace left");

        workspace.BeginDrag(tab);
        workspace.CompleteDrag(target);

        var snapshot = workspace.CaptureLayoutSnapshot();
        var root = Assert.IsType<EditorDockLayoutNodeSnapshot>(snapshot.Root);

        Assert.Equal(Orientation.Horizontal, root.Orientation);
        Assert.Equal(["center-secondary"], root.First?.TabIds);
        Assert.Equal("split-left-work", root.Second?.Id);
        Assert.Equal(0.2, root.FirstLength!.Value, precision: 10);
        Assert.Equal(0.8, root.SecondLength!.Value, precision: 10);
        Assert.Equal(root.First?.WindowId, snapshot.ActiveWindowId);
    }

    [Fact]
    public void Sequential_adjacent_inserts_normalize_nested_user_splits_without_reordering()
    {
        var workspace = new EditorDockWorkspaceViewModel(CreateLayoutRegistry());

        InsertCenterTabBesideCenter(workspace, "center-secondary");
        InsertCenterTabBesideCenter(workspace, "center-tertiary");

        var snapshot = workspace.CaptureLayoutSnapshot();

        Assert.Equal(
            ["left", "center-primary", "center-tertiary", "center-secondary", "bottom", "right"],
            CaptureWindowTabOrder(snapshot.Root));
    }

    [Fact]
    public void Capture_and_restore_round_trip_mutated_layout()
    {
        var workspace = new EditorDockWorkspaceViewModel(CreateLayoutRegistry());
        InsertCenterTabBesideCenter(workspace, "center-secondary");
        InsertCenterTabBesideCenter(workspace, "center-tertiary");
        var expected = workspace.CaptureLayoutSnapshot();
        var restoredWorkspace = new EditorDockWorkspaceViewModel(CreateLayoutRegistry());

        var restored = restoredWorkspace.RestoreLayoutSnapshot(expected);
        var actual = restoredWorkspace.CaptureLayoutSnapshot();

        Assert.True(restored);
        Assert.Equal(expected.ActiveWindowId, actual.ActiveWindowId);
        AssertLayoutEqual(expected.Root, actual.Root);
    }

    private static void InsertCenterTabBesideCenter(
        EditorDockWorkspaceViewModel workspace,
        string tabId)
    {
        var tab = workspace.CenterWindow.Tabs.Single(candidate => candidate.Id == tabId);
        var target = new EditorDockDropTarget(
            EditorDockDropOperation.InsertRight,
            EditorDockDropGuideKind.Insert,
            EditorDockArea.Center,
            workspace.CenterWindow.Id,
            PreviewBounds: default,
            Label: "Insert right");

        workspace.BeginDrag(tab);
        workspace.CompleteDrag(target);
    }

    private static EditorDockLayoutNodeSnapshot FindSnapshotById(
        EditorDockLayoutNodeSnapshot? node,
        string id)
    {
        Assert.NotNull(node);
        if (node.Id == id)
        {
            return node;
        }

        if (node.First is not null)
        {
            var firstMatch = TryFindSnapshotById(node.First, id);
            if (firstMatch is not null)
            {
                return firstMatch;
            }
        }

        if (node.Second is not null)
        {
            var secondMatch = TryFindSnapshotById(node.Second, id);
            if (secondMatch is not null)
            {
                return secondMatch;
            }
        }

        throw new InvalidOperationException($"Layout node '{id}' was not found.");
    }

    private static EditorDockLayoutNodeSnapshot? TryFindSnapshotById(
        EditorDockLayoutNodeSnapshot node,
        string id)
    {
        if (node.Id == id)
        {
            return node;
        }

        return node.First is null
            ? node.Second is null ? null : TryFindSnapshotById(node.Second, id)
            : TryFindSnapshotById(node.First, id)
                ?? (node.Second is null ? null : TryFindSnapshotById(node.Second, id));
    }

    private static List<string> CaptureWindowTabOrder(EditorDockLayoutNodeSnapshot? node)
    {
        var tabIds = new List<string>();
        CollectWindowTabOrder(node, tabIds);
        return tabIds;
    }

    private static void CollectWindowTabOrder(
        EditorDockLayoutNodeSnapshot? node,
        List<string> tabIds)
    {
        if (node is null)
        {
            return;
        }

        if (node.Kind == "Window")
        {
            tabIds.AddRange(node.TabIds);
            return;
        }

        CollectWindowTabOrder(node.First, tabIds);
        CollectWindowTabOrder(node.Second, tabIds);
    }

    private static void AssertLayoutEqual(
        EditorDockLayoutNodeSnapshot? expected,
        EditorDockLayoutNodeSnapshot? actual)
    {
        if (expected is null || actual is null)
        {
            Assert.Equal(expected, actual);
            return;
        }

        Assert.Equal(expected.Kind, actual.Kind);
        Assert.Equal(expected.Id, actual.Id);
        Assert.Equal(expected.WindowId, actual.WindowId);
        Assert.Equal(expected.WindowTitle, actual.WindowTitle);
        Assert.Equal(expected.WindowArea, actual.WindowArea);
        Assert.Equal(expected.WindowRole, actual.WindowRole);
        Assert.Equal(expected.TabIds, actual.TabIds);
        Assert.Equal(expected.ActiveTabId, actual.ActiveTabId);
        Assert.Equal(expected.Orientation, actual.Orientation);
        Assert.Equal(expected.FirstLength?.Value, actual.FirstLength?.Value);
        Assert.Equal(expected.FirstLength?.Unit, actual.FirstLength?.Unit);
        Assert.Equal(expected.SecondLength?.Value, actual.SecondLength?.Value);
        Assert.Equal(expected.SecondLength?.Unit, actual.SecondLength?.Unit);
        AssertLayoutEqual(expected.First, actual.First);
        AssertLayoutEqual(expected.Second, actual.Second);
    }

    private static PanelRegistry CreateLayoutRegistry()
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(
            "left",
            DockContentCachePolicy.KeepAlive,
            () => new object(),
            EditorDockArea.Left));
        registry.Register(CreateDescriptor(
            "center-primary",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "center-secondary",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "center-tertiary",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        registry.Register(CreateDescriptor(
            "bottom",
            DockContentCachePolicy.KeepAlive,
            () => new object(),
            EditorDockArea.Bottom));
        registry.Register(CreateDescriptor(
            "right",
            DockContentCachePolicy.KeepAlive,
            () => new object(),
            EditorDockArea.Right));
        return registry;
    }

    private static PanelRegistry CreateRegistry(
        string id,
        DockContentCachePolicy cachePolicy,
        Func<object> createContent,
        EditorDockArea area = EditorDockArea.Center)
    {
        var registry = new PanelRegistry();
        registry.Register(CreateDescriptor(id, cachePolicy, createContent, area));
        return registry;
    }

    private static PanelDescriptor CreateDescriptor(
        string id,
        DockContentCachePolicy cachePolicy,
        Func<object> createContent,
        EditorDockArea area = EditorDockArea.Center)
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

    private static EditorDockLayoutSnapshot CreateSinglePanelLayoutSnapshot(
        string panelId,
        string windowId)
    {
        return new EditorDockLayoutSnapshot
        {
            Version = 1,
            ActiveWindowId = windowId,
            Root = CreateSinglePanelLayoutNode(panelId, windowId),
        };
    }

    private static EditorDockFloatingWindowSnapshot CreateFloatingWindowSnapshot(
        string panelId,
        string windowId)
    {
        return new EditorDockFloatingWindowSnapshot
        {
            X = 16,
            Y = 24,
            Width = 480,
            Height = 320,
            ActiveWindowId = windowId,
            Root = CreateSinglePanelLayoutNode(panelId, windowId),
        };
    }

    private static EditorDockLayoutNodeSnapshot CreateSinglePanelLayoutNode(
        string panelId,
        string windowId)
    {
        return new EditorDockLayoutNodeSnapshot
        {
            Kind = "Window",
            Id = $"node-{windowId}",
            WindowId = windowId,
            WindowTitle = panelId,
            WindowArea = EditorDockArea.Center,
            WindowRole = "Test",
            TabIds = [panelId],
            ActiveTabId = panelId,
        };
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

    private sealed class RecordingFrameUpdateSink(
        EditorPanelFrameUpdateRequest frameUpdateRequest) : IEditorPanelFrameUpdateSink
    {
        public List<EditorPanelFrameContext> Frames { get; } = [];

        public EditorPanelFrameUpdateRequest FrameUpdateRequest { get; } = frameUpdateRequest;

        public void OnEditorPanelFrame(EditorPanelFrameContext context)
        {
            Frames.Add(context);
        }
    }

    private sealed class RecordingLifecycleFrameSink :
        IEditorPanelLifecycleSink,
        IEditorPanelFrameUpdateSink
    {
        public List<string> Events { get; } = [];

        public List<EditorPanelFrameContext> Frames { get; } = [];

        public EditorPanelFrameUpdateRequest FrameUpdateRequest =>
            EditorPanelFrameUpdateRequest.Active();

        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            Events.Add($"attached:{GetHostKind(context)}");
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            Events.Add($"activated:{GetHostKind(context)}");
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            Events.Add($"deactivated:{GetHostKind(context)}");
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            Events.Add($"detached:{GetHostKind(context)}");
        }

        public void OnEditorPanelFrame(EditorPanelFrameContext context)
        {
            Frames.Add(context);
        }

        private static string GetHostKind(EditorPanelLifecycleContext context)
        {
            return context.IsFloatingWorkspace ? "Floating" : "Main";
        }
    }

    private sealed class ThrowingWorkspacePanelSink :
        IEditorPanelLifecycleSink,
        IEditorPanelVisibilitySink,
        IEditorPanelFrameUpdateSink,
        IDisposable
    {
        public InvalidOperationException ActivatedFailure { get; } =
            new("activated failure");

        public InvalidOperationException AttachedFailure { get; } =
            new("attached failure");

        public InvalidOperationException DeactivatedFailure { get; } =
            new("deactivated failure");

        public InvalidOperationException HiddenFailure { get; } =
            new("hidden failure");

        public InvalidOperationException ShownFailure { get; } =
            new("shown failure");

        public InvalidOperationException DetachedFailure { get; } =
            new("detached failure");

        public InvalidOperationException DisposeFailure { get; } =
            new("dispose failure");

        public bool ThrowOnActivated { get; set; }

        public bool ThrowOnAttached { get; set; }

        public bool ThrowOnFloatingShown { get; set; }

        public bool ThrowOnShown { get; set; }

        public bool ThrowOnDeactivated { get; set; }

        public bool ThrowOnHidden { get; set; }

        public bool ThrowOnDetached { get; set; }

        public bool ThrowOnDispose { get; set; }

        public int DisposeCount { get; private set; }

        public int DetachedCount { get; private set; }

        public EditorPanelFrameUpdateRequest FrameUpdateRequest =>
            EditorPanelFrameUpdateRequest.Active();

        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            if (ThrowOnAttached)
            {
                throw AttachedFailure;
            }
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            if (ThrowOnActivated)
            {
                throw ActivatedFailure;
            }
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            if (ThrowOnDeactivated)
            {
                throw DeactivatedFailure;
            }
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            DetachedCount++;
            if (ThrowOnDetached)
            {
                throw DetachedFailure;
            }
        }

        public void OnPanelShown(EditorPanelLifecycleContext context)
        {
            if (ThrowOnShown
                || (ThrowOnFloatingShown && context.IsFloatingWorkspace))
            {
                throw ShownFailure;
            }
        }

        public void OnPanelHidden(EditorPanelLifecycleContext context)
        {
            if (ThrowOnHidden)
            {
                throw HiddenFailure;
            }
        }

        public void OnEditorPanelFrame(EditorPanelFrameContext context)
        {
        }

        public void Dispose()
        {
            DisposeCount++;
            if (ThrowOnDispose)
            {
                throw DisposeFailure;
            }
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

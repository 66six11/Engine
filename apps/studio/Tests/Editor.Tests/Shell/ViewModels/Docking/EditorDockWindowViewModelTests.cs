using System;
using System.Collections.Generic;
using System.Globalization;
using Asharia.Editor.Panels;
using Editor.Core.Models.Panels;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Docking;

public sealed class EditorDockWindowViewModelTests
{
    [Fact]
    public void Remove_clears_removed_active_tab_state()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        window.Add(first);
        window.Add(second);

        window.Remove(first);

        Assert.False(first.IsActive);
        Assert.False(first.IsDragSource);
        Assert.Same(second, window.ActiveTab);
        Assert.True(second.IsActive);
    }

    [Fact]
    public void Host_focus_controls_active_tab_focus_indicator()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var tab = CreateTab("tab");
        window.Add(tab);
        window.SetActiveWindowState(true);
        var tabStripItem = window.TabStripItems[0];

        Assert.True(tabStripItem.IsSelectedInFocusedWindow);
        Assert.False(tabStripItem.IsSelectedInInactiveWindow);

        window.SetHostFocusState(false);

        Assert.False(tabStripItem.IsSelectedInFocusedWindow);
        Assert.True(tabStripItem.IsSelectedInInactiveWindow);

        window.SetHostFocusState(true);

        Assert.True(tabStripItem.IsSelectedInFocusedWindow);
        Assert.False(tabStripItem.IsSelectedInInactiveWindow);
    }

    [Fact]
    public void HideDragSourceTab_collapses_source_tab_until_cleared()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        window.Add(first);
        window.Add(second);

        Assert.True(window.HideDragSourceTab(first));

        var visibleItem = Assert.Single(window.TabStripItems);
        Assert.Same(second, visibleItem.Tab);

        Assert.True(window.ClearHiddenDragSourceTab());

        Assert.Equal(2, window.TabStripItems.Count);
        Assert.Same(first, window.TabStripItems[0].Tab);
        Assert.Same(second, window.TabStripItems[1].Tab);
        Assert.False(window.TabStripItems[0].IsSourceGhost);
        Assert.False(window.TabStripItems[1].IsSourceGhost);
    }

    [Fact]
    public void ShowLocalTabReorderPreview_collapses_source_tab_and_inserts_placeholder()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var first = CreateTab("first");
        var second = CreateTab("second");
        var third = CreateTab("third");
        window.Add(first);
        window.Add(second);
        window.Add(third);

        Assert.True(window.ShowLocalTabReorderPreview(first, 2, showsTab: false));
        Assert.False(window.ShowLocalTabReorderPreview(first, 2, showsTab: false));

        Assert.Equal(3, window.TabStripItems.Count);
        Assert.Same(second, window.TabStripItems[0].Tab);
        Assert.Same(first, window.TabStripItems[1].Tab);
        Assert.True(window.TabStripItems[1].IsPlaceholder);
        Assert.Same(third, window.TabStripItems[2].Tab);

        Assert.True(window.ClearLocalTabReorderPreview());

        Assert.Equal(3, window.TabStripItems.Count);
        Assert.Same(first, window.TabStripItems[0].Tab);
        Assert.Same(second, window.TabStripItems[1].Tab);
        Assert.Same(third, window.TabStripItems[2].Tab);
        Assert.All(window.TabStripItems, item => Assert.False(item.IsPlaceholder));
    }

    [Fact]
    public void Tab_selection_routes_visibility_without_activating_new_tab()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var firstSink = new RecordingPanelSink();
        var secondSink = new RecordingPanelSink();
        var first = CreateTab("first", firstSink);
        var second = CreateTab("second", secondSink);
        first.AttachPanelInstance(isFloatingWorkspace: false);
        second.AttachPanelInstance(isFloatingWorkspace: false);
        window.Add(first);
        window.Add(second);
        first.ActivatePanelInstance();
        firstSink.Events.Clear();
        secondSink.Events.Clear();

        window.Activate(second);

        Assert.Equal(["deactivated:first", "hidden:first"], firstSink.Events);
        Assert.Equal(["shown:second"], secondSink.Events);

        firstSink.Events.Clear();
        secondSink.Events.Clear();
        window.Remove(second);

        Assert.Equal(["shown:first"], firstSink.Events);
        Assert.Equal(["hidden:second"], secondSink.Events);
    }

    [Fact]
    public void Panel_layout_is_reported_only_while_shown_and_deduplicates_exact_geometry()
    {
        var sink = new RecordingPanelSink();
        var tab = CreateTab("panel", sink);
        tab.AttachPanelInstance(isFloatingWorkspace: false);
        tab.ShowPanelInstance();
        sink.Events.Clear();

        tab.UpdatePanelLayout(640, 480, 1.25);
        tab.UpdatePanelLayout(640, 480, 1.25);
        tab.UpdatePanelLayout(640, 480, 1.5);
        tab.HidePanelInstance();
        tab.UpdatePanelLayout(800, 600, 1.5);
        tab.ShowPanelInstance();
        tab.UpdatePanelLayout(640, 480, 1.5);

        Assert.Equal(
            [
                "layout:panel:640x480@1.25",
                "layout:panel:640x480@1.5",
                "hidden:panel",
                "shown:panel",
                "layout:panel:640x480@1.5",
            ],
            sink.Events);
    }

    [Fact]
    public void Remove_keeps_window_collections_consistent_when_hide_callback_fails()
    {
        var window = new EditorDockWindowViewModel("window", "Window", EditorDockArea.Center, "Test");
        var firstSink = new RecordingPanelSink();
        var secondSink = new RecordingPanelSink();
        var first = CreateTab("first", firstSink);
        var second = CreateTab("second", secondSink);
        first.AttachPanelInstance(isFloatingWorkspace: false);
        second.AttachPanelInstance(isFloatingWorkspace: false);
        window.Add(first);
        window.Add(second);
        first.ActivatePanelInstance();
        firstSink.ThrowOnHidden = true;
        secondSink.Events.Clear();

        var exception = Assert.Throws<InvalidOperationException>(() => window.Remove(first));

        Assert.Same(firstSink.HiddenFailure, exception);
        Assert.DoesNotContain(first, window.Tabs);
        Assert.Single(window.Tabs);
        Assert.Same(second, window.ActiveTab);
        Assert.True(second.IsActive);
        Assert.Contains("shown:second", secondSink.Events);
        Assert.All(window.TabStripItems, item => Assert.NotSame(first, item.Tab));
    }

    private static EditorDockTabViewModel CreateTab(string id, object? content = null)
    {
        return new EditorDockTabViewModel(
            id,
            id,
            "TEST",
            id,
            "idle",
            PanelKind.Tool,
            EditorDockArea.Center,
            content ?? new object());
    }

    private sealed class RecordingPanelSink :
        IEditorPanelLifecycleSink,
        IEditorPanelVisibilitySink,
        IEditorPanelLayoutSink
    {
        public List<string> Events { get; } = [];

        public InvalidOperationException HiddenFailure { get; } =
            new("hidden failure");

        public bool ThrowOnHidden { get; set; }

        public void OnPanelAttached(EditorPanelLifecycleContext context)
        {
            Events.Add($"attached:{context.PanelId}");
        }

        public void OnPanelActivated(EditorPanelLifecycleContext context)
        {
            Events.Add($"activated:{context.PanelId}");
        }

        public void OnPanelDeactivated(EditorPanelLifecycleContext context)
        {
            Events.Add($"deactivated:{context.PanelId}");
        }

        public void OnPanelDetached(EditorPanelLifecycleContext context)
        {
            Events.Add($"detached:{context.PanelId}");
        }

        public void OnPanelShown(EditorPanelLifecycleContext context)
        {
            Events.Add($"shown:{context.PanelId}");
        }

        public void OnPanelHidden(EditorPanelLifecycleContext context)
        {
            Events.Add($"hidden:{context.PanelId}");
            if (ThrowOnHidden)
            {
                throw HiddenFailure;
            }
        }

        public void OnPanelLayoutChanged(EditorPanelLayoutContext context)
        {
            Events.Add(
                $"layout:{context.Panel.PanelId}:"
                + $"{context.LogicalWidth.ToString(CultureInfo.InvariantCulture)}x"
                + $"{context.LogicalHeight.ToString(CultureInfo.InvariantCulture)}@"
                + context.RenderScale.ToString(CultureInfo.InvariantCulture));
        }
    }
}

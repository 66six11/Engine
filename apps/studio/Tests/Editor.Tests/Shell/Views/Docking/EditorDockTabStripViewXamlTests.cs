using System;
using System.Collections.Generic;
using System.IO;
using Editor.Shell.Views.Docking;
using Xunit;

namespace Editor.Tests.Shell.Views.Docking;

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
        var source = LoadSource("Shell", "Views", "Docking", "EditorDockTabStripView.axaml.cs");

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

        Assert.DoesNotContain("xmlns:icons=\"using:Editor.UI.Icons\"", xaml);
        Assert.DoesNotContain("IconKey=\"studio.ui.chevron-left\"", xaml);
        Assert.DoesNotContain("IconKey=\"studio.ui.chevron-right\"", xaml);
        Assert.DoesNotContain("owned-dock-tab-overflow-icon", xaml);
    }

    [Fact]
    public void Tab_strip_exposes_visible_viewport_bounds_for_hit_testing()
    {
        var source = LoadSource("Shell", "Views", "Docking", "EditorDockTabStripView.axaml.cs");

        Assert.Contains("internal bool TryGetViewportBounds(Visual relativeTo, out Rect bounds)", source);
        Assert.Contains("DockTabStripScrollViewer.TranslatePoint(new Point(0, 0), relativeTo)", source);
        Assert.Contains("new Rect(origin.Value, DockTabStripScrollViewer.Bounds.Size)", source);
    }

    [Fact]
    public void Workspace_uses_visible_tab_strip_viewport_for_tab_well_bounds()
    {
        var source = LoadSource("Shell", "Views", "Docking", "EditorDockWorkspaceView.axaml.cs");

        Assert.Contains("tabStrip.TryGetViewportBounds(DockRoot, out var viewportBounds)", source);
        Assert.Contains("return viewportBounds;", source);
        Assert.Contains("GetTabContentOriginX(host, tabWellBounds.X)", source);
    }

    [Fact]
    public void Workspace_close_tab_always_checks_for_empty_floating_host()
    {
        var source = LoadSource(
            "Shell",
            "Views",
            "Docking",
            "EditorDockWorkspaceView.axaml.cs");
        var method = GetMethod(source, "CloseTab");

        Assert.Contains("exceptions.Capture(() => workspace.CloseTab(tab))", method);
        Assert.Contains("exceptions.Capture(() => CloseEmptyFloatingHost(workspace))", method);
        Assert.Contains("exceptions.ThrowIfAny();", method);
    }

    [Fact]
    public void Tab_activation_moves_keyboard_focus_to_dock_panel_body()
    {
        var xaml = LoadSource("Shell", "Views", "Docking", "EditorDockWindowView.axaml");
        var source = LoadSource("Shell", "Views", "Docking", "EditorDockWindowView.axaml.cs");

        Assert.Contains("Focusable=\"True\"", GetDockPanelBodyElement(xaml));
        Assert.Contains("FocusDockPanelBodyForTabActivation();", source);
        Assert.Contains("DockPanelBody.Focus(NavigationMethod.Pointer)", source);
    }

    [Fact]
    public void Panel_content_host_reports_post_layout_geometry_and_scaling()
    {
        var xaml = LoadSource("Shell", "Views", "Docking", "EditorDockWindowView.axaml");
        var source = LoadSource(
            "Shell",
            "Views",
            "Docking",
            "EditorDockPanelContentHost.cs");

        Assert.Contains(
            "<views:EditorDockPanelContentHost Panel=\"{Binding ActiveTab}\"",
            xaml);
        Assert.Contains("protected override Size ArrangeOverride(Size finalSize)", source);
        Assert.Contains("var arrangedSize = base.ArrangeOverride(finalSize);", source);
        Assert.Contains("QueueLayoutNotification();", GetMethod(source, "ArrangeOverride"));
        Assert.DoesNotContain("UpdatePanelLayout(", GetMethod(source, "ArrangeOverride"));
        Assert.Contains("InvalidateArrange();", source);
        Assert.Contains("QueueLayoutNotification();", GetMethod(source, "OnPropertyChanged"));
        Assert.Contains(
            "Dispatcher.UIThread.Post(action, DispatcherPriority.Loaded)",
            source);
        Assert.Contains("topLevel_.ScalingChanged += OnTopLevelScalingChanged;", source);
        Assert.Contains("topLevel_.ScalingChanged -= OnTopLevelScalingChanged;", source);
        Assert.Contains(
            "QueueLayoutNotification();",
            GetMethod(source, "OnTopLevelScalingChanged"));
        Assert.Contains("layoutNotificationQueue_.Cancel();", GetMethod(source, "OnDetachedFromVisualTree"));
        Assert.Contains("var logicalSize = Bounds.Size;", GetMethod(source, "PublishCurrentLayout"));
        Assert.Contains("topLevel_?.RenderScaling ?? 1d", source);
    }

    [Fact]
    public void Panel_content_host_coalesces_rapid_layout_notifications_to_latest_state()
    {
        var callbacks = new List<Action>();
        var publishedValues = new List<int>();
        var latestValue = 0;
        var queue = new EditorDockPanelLayoutNotificationQueue(
            callbacks.Add,
            () => publishedValues.Add(latestValue));

        latestValue = 1;
        queue.Request();
        latestValue = 2;
        queue.Request();
        latestValue = 3;
        queue.Request();

        Assert.Single(callbacks);
        Assert.Empty(publishedValues);

        callbacks[0]();

        Assert.Equal([3], publishedValues);
    }

    [Fact]
    public void Panel_content_host_cancels_stale_layout_notification_before_requeue()
    {
        var callbacks = new List<Action>();
        var publishedValues = new List<int>();
        var latestValue = 0;
        var queue = new EditorDockPanelLayoutNotificationQueue(
            callbacks.Add,
            () => publishedValues.Add(latestValue));

        latestValue = 1;
        queue.Request();
        queue.Cancel();
        latestValue = 2;
        queue.Request();

        Assert.Equal(2, callbacks.Count);

        callbacks[0]();
        Assert.Empty(publishedValues);

        callbacks[1]();
        Assert.Equal([2], publishedValues);
    }

    [Fact]
    public void Panel_content_host_isolates_layout_callback_failure_and_accepts_next_notification()
    {
        var callbacks = new List<Action>();
        var reportedFailures = new List<Exception>();
        var expectedFailure = new InvalidOperationException("Injected layout callback failure.");
        var publishCount = 0;
        var queue = new EditorDockPanelLayoutNotificationQueue(
            callbacks.Add,
            () =>
            {
                publishCount++;
                if (publishCount == 1)
                {
                    throw expectedFailure;
                }
            },
            reportedFailures.Add);

        queue.Request();

        Assert.Null(Record.Exception(callbacks[0]));
        Assert.Same(expectedFailure, Assert.Single(reportedFailures));

        queue.Request();

        Assert.Equal(2, callbacks.Count);
        Assert.Null(Record.Exception(callbacks[1]));
        Assert.Equal(2, publishCount);
        Assert.Single(reportedFailures);
    }

    private static string LoadTabStripXaml()
    {
        return LoadSource("Shell", "Views", "Docking", "EditorDockTabStripView.axaml");
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

    private static string GetDockPanelBodyElement(string xaml)
    {
        const string selector = "x:Name=\"DockPanelBody\"";
        var nameIndex = xaml.IndexOf(selector, StringComparison.Ordinal);
        if (nameIndex < 0)
        {
            return string.Empty;
        }

        var start = xaml.LastIndexOf("<Border", nameIndex, StringComparison.Ordinal);
        if (start < 0)
        {
            return string.Empty;
        }

        var end = xaml.IndexOf(">", nameIndex, StringComparison.Ordinal);
        return end < 0 ? xaml[start..] : xaml[start..(end + 1)];
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

    private static string GetMethod(string source, string methodName)
    {
        var signatureIndex = source.IndexOf($"{methodName}(", StringComparison.Ordinal);
        if (signatureIndex < 0)
        {
            return string.Empty;
        }

        var openingBraceIndex = source.IndexOf('{', signatureIndex);
        if (openingBraceIndex < 0)
        {
            return string.Empty;
        }

        var depth = 0;
        for (var index = openingBraceIndex; index < source.Length; index++)
        {
            depth += source[index] switch
            {
                '{' => 1,
                '}' => -1,
                _ => 0,
            };

            if (depth == 0)
            {
                return source[openingBraceIndex..(index + 1)];
            }
        }

        return source[openingBraceIndex..];
    }
}

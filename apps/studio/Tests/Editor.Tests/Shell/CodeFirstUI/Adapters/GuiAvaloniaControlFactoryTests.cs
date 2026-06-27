using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Editor.Shell.CodeFirstUI;
using Editor.Shell.CodeFirstUI.Adapters;
using Xunit;

namespace Editor.Tests.Shell.CodeFirstUI.Adapters;

public sealed class GuiAvaloniaControlFactoryTests
{
    [Fact]
    public void Build_maps_split_with_catalog_list_to_grid_and_list_box()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            using (builder.Panel("catalog", "Catalog"))
            {
                builder.List(
                    "sections",
                    [new GuiListItem("overview", "Overview"), new GuiListItem("buttons", "Buttons")],
                    "overview");
            }

            using (builder.Panel("preview", "Preview"))
            {
                builder.Label("title", "Overview");
            }
        }

        var factory = new GuiAvaloniaControlFactory(new NoopCodeFirstPanelHost());

        var control = factory.Build(builder.Build());

        var grid = Assert.IsType<Grid>(control);
        Assert.Equal(3, grid.Children.Count);
        Assert.Equal(3, grid.ColumnDefinitions.Count);
        var splitter = Assert.Single(grid.Children.OfType<GridSplitter>());
        Assert.Equal(1, Grid.GetColumn(splitter));
        Assert.Equal(GridResizeDirection.Columns, splitter.ResizeDirection);
        Assert.Contains("code-first-splitter", splitter.Classes);
        Assert.Contains("vertical", splitter.Classes);
        Assert.NotNull(FindDescendant<ListBox>(grid));
        Assert.NotNull(FindDescendant<TextBlock>(grid, text => text.Text == "Overview"));
    }

    [Fact]
    public void Build_panel_constrains_list_rows_for_virtualized_content()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Panel("catalog", "Catalog"))
        {
            builder.Label("hint", "Sections");
            builder.List(
                "sections",
                Enumerable.Range(0, 100)
                    .Select(index => new GuiListItem($"section-{index}", $"Section {index}"))
                    .ToArray(),
                "section-0");
        }

        var factory = new GuiAvaloniaControlFactory(new NoopCodeFirstPanelHost());

        var border = Assert.IsType<Border>(factory.Build(builder.Build()));
        var panelGrid = Assert.IsType<Grid>(border.Child);
        var body = Assert.IsType<Grid>(
            panelGrid.Children.Single(child => Grid.GetRow(child) == 1));

        Assert.Equal(2, body.RowDefinitions.Count);
        Assert.Equal(GridUnitType.Auto, body.RowDefinitions[0].Height.GridUnitType);
        Assert.Equal(GridUnitType.Star, body.RowDefinitions[1].Height.GridUnitType);
        var listBox = Assert.IsType<ListBox>(
            body.Children.Single(child => Grid.GetRow(child) == 1));
        Assert.Equal(VerticalAlignment.Stretch, listBox.VerticalAlignment);
    }

    [Fact]
    public void Build_maps_scroll_and_validation_message_to_feedback_controls()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Panel("preview", "Preview"))
        {
            using (builder.Scroll("details"))
            {
                builder.ValidationMessage(
                    "missing-shader",
                    "Shader metadata is missing.",
                    EditorDiagnosticSeverity.Warning);
            }
        }

        var factory = new GuiAvaloniaControlFactory(new NoopCodeFirstPanelHost());

        var control = factory.Build(builder.Build());

        var scrollViewer = FindDescendant<ScrollViewer>(control);
        Assert.NotNull(scrollViewer);
        Assert.Equal(ScrollBarVisibility.Disabled, scrollViewer!.HorizontalScrollBarVisibility);
        Assert.Equal(ScrollBarVisibility.Auto, scrollViewer.VerticalScrollBarVisibility);
        Assert.Equal(VerticalAlignment.Stretch, scrollViewer.VerticalAlignment);

        var message = FindDescendant<TextBlock>(
            control,
            text => text.Text == "Shader metadata is missing.");
        Assert.NotNull(message);
        Assert.Equal(Avalonia.Media.TextWrapping.Wrap, message!.TextWrapping);
        Assert.Contains("code-first-validation-message", message.Classes);
        Assert.Contains("warning", message.Classes);
    }

    [Fact]
    public void Splitter_width_changes_are_reported_to_host_as_split_ratio()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            builder.Label("catalog", "Catalog");
            builder.Label("preview", "Preview");
        }

        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);
        var grid = Assert.IsType<Grid>(factory.Build(builder.Build()));

        grid.ColumnDefinitions[0].Width = new GridLength(0.40d, GridUnitType.Star);
        grid.ColumnDefinitions[2].Width = new GridLength(0.60d, GridUnitType.Star);

        var resize = Assert.Single(host.SplitResizes, resize => resize.Ratio == 0.40d);
        Assert.Equal(new GuiNodeId("ui-style", "layout", GuiNodeKind.Split), resize.NodeId);
    }

    [Fact]
    public void Build_maps_text_field_and_toggle_and_reports_draft_input_changes()
    {
        var builder = new GuiFrameBuilder("ui-style");
        builder.TextField("filter", "Filter", "gbuffer");
        builder.Toggle("show-disabled", "Show Disabled", isChecked: true);

        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);

        var control = factory.Build(builder.Build());

        var textBox = FindDescendant<TextBox>(
            control,
            textBox => textBox.Text == "gbuffer");
        var checkBox = FindDescendant<CheckBox>(
            control,
            checkBox => checkBox.IsChecked == true);
        Assert.NotNull(textBox);
        Assert.NotNull(checkBox);

        textBox!.Text = "albedo";
        checkBox!.IsChecked = false;

        var textChange = Assert.Single(host.TextChanges);
        Assert.Equal(new GuiNodeId("ui-style", "filter", GuiNodeKind.TextField), textChange.NodeId);
        Assert.Equal("albedo", textChange.Text);
        Assert.Empty(host.TextCommits);

        var toggleChange = Assert.Single(host.ToggleChanges);
        Assert.Equal(new GuiNodeId("ui-style", "show-disabled", GuiNodeKind.Toggle), toggleChange.NodeId);
        Assert.False(toggleChange.IsChecked);
    }

    [Fact]
    public void Text_field_on_change_commit_mode_commits_each_text_change()
    {
        var builder = new GuiFrameBuilder("ui-style");
        builder.TextField("filter", "Filter", "gbuffer", GuiTextInputCommitMode.OnChange);
        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);
        var textBox = FindDescendant<TextBox>(factory.Build(builder.Build()));
        Assert.NotNull(textBox);

        textBox!.Text = "albedo";

        var commit = Assert.Single(host.TextCommits);
        Assert.Equal(new GuiNodeId("ui-style", "filter", GuiNodeKind.TextField), commit.NodeId);
        Assert.Equal("albedo", commit.Text);
        Assert.Empty(host.TextChanges);
    }

    [Fact]
    public void Text_field_lost_focus_commit_mode_commits_on_focus_loss()
    {
        var builder = new GuiFrameBuilder("ui-style");
        builder.TextField("filter", "Filter", "gbuffer", GuiTextInputCommitMode.OnLostFocus);
        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);
        var textBox = FindDescendant<TextBox>(factory.Build(builder.Build()));
        Assert.NotNull(textBox);

        textBox!.Text = "albedo";
        textBox.RaiseEvent(new FocusChangedEventArgs(InputElement.LostFocusEvent)
        {
            Source = textBox,
        });

        Assert.Single(host.TextChanges);
        var commit = Assert.Single(host.TextCommits);
        Assert.Equal("albedo", commit.Text);
    }

    [Fact]
    public void Text_field_enter_commit_mode_commits_on_enter_key()
    {
        var builder = new GuiFrameBuilder("ui-style");
        builder.TextField("filter", "Filter", "gbuffer", GuiTextInputCommitMode.OnEnter);
        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);
        var textBox = FindDescendant<TextBox>(factory.Build(builder.Build()));
        Assert.NotNull(textBox);

        textBox!.Text = "albedo";
        textBox.RaiseEvent(new KeyEventArgs
        {
            RoutedEvent = InputElement.KeyDownEvent,
            Key = Key.Enter,
            Source = textBox,
        });

        Assert.Single(host.TextChanges);
        var commit = Assert.Single(host.TextCommits);
        Assert.Equal("albedo", commit.Text);
    }

    [Fact]
    public void Text_field_debounced_commit_mode_defers_and_coalesces_commits()
    {
        var builder = new GuiFrameBuilder("ui-style");
        builder.TextField(
            "filter",
            "Filter",
            "gbuffer",
            GuiTextInputCommitMode.Debounced,
            TimeSpan.FromMilliseconds(150));
        var host = new RecordingCodeFirstPanelHost();
        var scheduler = new RecordingTextCommitScheduler();
        var factory = new GuiAvaloniaControlFactory(host, scheduler);
        var textBox = FindDescendant<TextBox>(factory.Build(builder.Build()));
        Assert.NotNull(textBox);

        textBox!.Text = "albedo";
        textBox.Text = "albedo roughness";

        Assert.Equal(
            ["albedo", "albedo roughness"],
            host.TextChanges.Select(change => change.Text).ToArray());
        Assert.Empty(host.TextCommits);
        Assert.Equal(TimeSpan.FromMilliseconds(150), scheduler.LastDelay);

        scheduler.RunPending();

        var commit = Assert.Single(host.TextCommits);
        Assert.Equal("albedo roughness", commit.Text);
    }

    private static T? FindDescendant<T>(Control control, Predicate<T>? predicate = null)
        where T : Control
    {
        if (control is T typed
            && (predicate is null || predicate(typed)))
        {
            return typed;
        }

        return control switch
        {
            Panel panel => panel.Children.Select(child => FindDescendant(child, predicate)).FirstOrDefault(found => found is not null),
            ContentControl content when content.Content is Control child => FindDescendant(child, predicate),
            Decorator decorator when decorator.Child is Control child => FindDescendant(child, predicate),
            _ => null,
        };
    }

    private sealed class NoopCodeFirstPanelHost : IGuiAvaloniaHost
    {
        public void ClickButton(GuiNodeId nodeId)
        {
        }

        public void SelectListItem(GuiNodeId nodeId, string itemId)
        {
        }

        public void ResizeSplit(GuiNodeId nodeId, double ratio)
        {
        }

        public void SetText(GuiNodeId nodeId, string text)
        {
        }

        public void CommitText(GuiNodeId nodeId, string text)
        {
        }

        public void SetToggle(GuiNodeId nodeId, bool isChecked)
        {
        }
    }

    private sealed class RecordingCodeFirstPanelHost : IGuiAvaloniaHost
    {
        private readonly List<SplitResize> splitResizes_ = [];
        private readonly List<TextChange> textChanges_ = [];
        private readonly List<TextChange> textCommits_ = [];
        private readonly List<ToggleChange> toggleChanges_ = [];

        public IReadOnlyList<SplitResize> SplitResizes => splitResizes_;

        public IReadOnlyList<TextChange> TextChanges => textChanges_;

        public IReadOnlyList<TextChange> TextCommits => textCommits_;

        public IReadOnlyList<ToggleChange> ToggleChanges => toggleChanges_;

        public void ClickButton(GuiNodeId nodeId)
        {
        }

        public void SelectListItem(GuiNodeId nodeId, string itemId)
        {
        }

        public void ResizeSplit(GuiNodeId nodeId, double ratio)
        {
            splitResizes_.Add(new SplitResize(nodeId, ratio));
        }

        public void SetText(GuiNodeId nodeId, string text)
        {
            textChanges_.Add(new TextChange(nodeId, text));
        }

        public void CommitText(GuiNodeId nodeId, string text)
        {
            textCommits_.Add(new TextChange(nodeId, text));
        }

        public void SetToggle(GuiNodeId nodeId, bool isChecked)
        {
            toggleChanges_.Add(new ToggleChange(nodeId, isChecked));
        }
    }

    private sealed record SplitResize(
        GuiNodeId NodeId,
        double Ratio);

    private sealed record TextChange(
        GuiNodeId NodeId,
        string Text);

    private sealed record ToggleChange(
        GuiNodeId NodeId,
        bool IsChecked);

    private sealed class RecordingTextCommitScheduler : IGuiTextCommitScheduler
    {
        private ScheduledAction? pendingAction_;

        public TimeSpan? LastDelay { get; private set; }

        public IDisposable Schedule(TimeSpan delay, Action action)
        {
            LastDelay = delay;
            var scheduledAction = new ScheduledAction(action);
            pendingAction_ = scheduledAction;
            return new DisposableAction(() => scheduledAction.IsCanceled = true);
        }

        public void RunPending()
        {
            var action = pendingAction_;
            pendingAction_ = null;
            if (action is { IsCanceled: false })
            {
                action.Action();
            }
        }

        private sealed class ScheduledAction(Action action)
        {
            public Action Action { get; } = action;

            public bool IsCanceled { get; set; }
        }
    }

    private sealed class DisposableAction(Action dispose) : IDisposable
    {
        private bool isDisposed_;

        public void Dispose()
        {
            if (isDisposed_)
            {
                return;
            }

            dispose();
            isDisposed_ = true;
        }
    }
}

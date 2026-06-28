using System;
using System.Linq;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI;

public sealed class GuiFrameBuilderTests
{
    [Fact]
    public void Build_creates_ordered_tree_with_stable_key_paths()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");

        builder.Label("title", "RenderGraph");
        using (builder.Toolbar("toolbar"))
        {
            builder.Button("capture", "Capture Frame");
        }

        var tree = builder.Build();

        Assert.Equal("render.frameDebugger", tree.PanelId);
        Assert.Equal(GuiNodeKind.Root, tree.Root.Kind);
        Assert.Equal("render.frameDebugger", tree.Root.Id.FullKeyPath);

        var title = tree.Root.Children[0];
        Assert.Equal(GuiNodeKind.Label, title.Kind);
        Assert.Equal("title", title.Id.KeyPath);
        Assert.Equal("render.frameDebugger/title", title.Id.FullKeyPath);
        Assert.Equal("RenderGraph", title.Label);

        var toolbar = tree.Root.Children[1];
        Assert.Equal(GuiNodeKind.Toolbar, toolbar.Kind);
        Assert.Equal("toolbar", toolbar.Id.KeyPath);

        var capture = Assert.Single(toolbar.Children);
        Assert.Equal(GuiNodeKind.Button, capture.Kind);
        Assert.Equal("toolbar/capture", capture.Id.KeyPath);
        Assert.Equal("render.frameDebugger/toolbar/capture", capture.Id.FullKeyPath);
        Assert.Equal("Capture Frame", capture.Label);
    }

    [Fact]
    public void Build_preserves_label_tone_and_size_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");

        builder.Label("title", "Typography", GuiTextTone.Primary, GuiTextSize.Title);

        var label = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Label, label.Kind);
        Assert.Equal("Typography", label.Label);
        Assert.Equal(GuiTextTone.Primary, label.Payload.TextTone);
        Assert.Equal(GuiTextSize.Title, label.Payload.TextSize);
    }

    [Fact]
    public void Build_preserves_separator_nodes_for_toolbar_grouping()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");

        using (builder.Toolbar("toolbar"))
        {
            builder.Button("capture", "Capture Frame");
            builder.Separator("capture-group");
            builder.Button("clear", "Clear");
        }

        var toolbar = Assert.Single(builder.Build().Root.Children);

        Assert.Equal(
            [GuiNodeKind.Button, GuiNodeKind.Separator, GuiNodeKind.Button],
            toolbar.Children.Select(child => child.Kind).ToArray());
        Assert.Equal("toolbar/capture-group", toolbar.Children[1].Id.KeyPath);
        Assert.Null(toolbar.Children[1].Label);
    }

    [Fact]
    public void Build_preserves_navigation_view_payload_and_children()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var pages = new[]
        {
            new GuiNavigationItem("overview", "Overview"),
            new GuiNavigationItem("render/debug/frame-debugger", "Frame Debugger"),
        };

        using (builder.NavigationView(
            "catalog",
            pages,
            "render/debug/frame-debugger",
            0.25d,
            ["overview"]))
        {
            builder.Label("title", "Frame Debugger");
        }

        var navigation = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.NavigationView, navigation.Kind);
        Assert.Equal("catalog", navigation.Id.KeyPath);
        Assert.Equal(pages, navigation.Payload.NavigationItems);
        Assert.Equal("render/debug/frame-debugger", navigation.Payload.SelectedRoute);
        Assert.Equal(0.25d, navigation.Payload.SplitRatio);
        Assert.Equal(["overview"], navigation.Payload.CollapsedNavigationRoutes);

        var child = Assert.Single(navigation.Children);
        Assert.Equal("catalog/title", child.Id.KeyPath);
        Assert.Equal("Frame Debugger", child.Label);
    }

    [Fact]
    public void Build_preserves_split_panel_and_list_payloads()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var sections = new[]
        {
            new GuiListItem("overview", "Overview"),
            new GuiListItem("buttons", "Buttons"),
        };

        using (builder.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            using (builder.Panel("catalog", "Catalog"))
            {
                builder.List("sections", sections, "buttons");
            }

            using (builder.Panel("preview", "Preview"))
            {
                builder.Label("title", "Buttons");
            }
        }

        var split = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Split, split.Kind);
        Assert.Equal(GuiSplitDirection.Horizontal, split.Payload.SplitDirection);
        Assert.Equal(0.30d, split.Payload.SplitRatio);

        var catalog = split.Children[0];
        Assert.Equal(GuiNodeKind.Panel, catalog.Kind);
        Assert.Equal("Catalog", catalog.Label);

        var list = Assert.Single(catalog.Children);
        Assert.Equal(GuiNodeKind.List, list.Kind);
        Assert.Equal("buttons", list.Payload.SelectedItemId);
        Assert.Equal(sections, list.Payload.ListItems);
    }

    [Fact]
    public void Build_preserves_text_field_and_toggle_payloads()
    {
        var builder = new GuiFrameBuilder("ui.style");

        builder.TextField("filter", "Filter", "gbuffer", GuiTextInputCommitMode.OnEnter);
        builder.Toggle("show-disabled", "Show Disabled", isChecked: true);

        var tree = builder.Build();

        var textField = tree.Root.Children[0];
        Assert.Equal(GuiNodeKind.TextField, textField.Kind);
        Assert.Equal("Filter", textField.Label);
        Assert.Equal("gbuffer", textField.Payload.TextValue);
        Assert.Equal(GuiTextInputCommitMode.OnEnter, textField.Payload.TextCommitMode);

        var toggle = tree.Root.Children[1];
        Assert.Equal(GuiNodeKind.Toggle, toggle.Kind);
        Assert.Equal("Show Disabled", toggle.Label);
        Assert.True(toggle.Payload.IsChecked);
    }

    [Fact]
    public void Build_preserves_combo_box_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var modes = new[]
        {
            new GuiListItem("forward", "Forward"),
            new GuiListItem("deferred", "Deferred"),
        };

        builder.ComboBox("render-mode", "Render Mode", modes, "deferred");

        var comboBox = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.ComboBox, comboBox.Kind);
        Assert.Equal("Render Mode", comboBox.Label);
        Assert.Equal(modes, comboBox.Payload.ListItems);
        Assert.Equal("deferred", comboBox.Payload.SelectedItemId);
    }

    [Fact]
    public void Build_preserves_slider_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");

        builder.Slider(
            "exposure",
            "Exposure",
            value: 0.75d,
            minimum: 0d,
            maximum: 2d,
            smallChange: 0.05d,
            largeChange: 0.25d);

        var slider = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Slider, slider.Kind);
        Assert.Equal("Exposure", slider.Label);
        Assert.Equal(0.75d, slider.Payload.NumericValue);
        Assert.Equal(0d, slider.Payload.NumericMinimum);
        Assert.Equal(2d, slider.Payload.NumericMaximum);
        Assert.Equal(0.05d, slider.Payload.NumericSmallChange);
        Assert.Equal(0.25d, slider.Payload.NumericLargeChange);
    }

    [Fact]
    public void Build_preserves_debounced_text_field_commit_delay()
    {
        var builder = new GuiFrameBuilder("ui.style");

        builder.TextField(
            "filter",
            "Filter",
            "gbuffer",
            GuiTextInputCommitMode.Debounced,
            TimeSpan.FromMilliseconds(180));

        var textField = Assert.Single(builder.Build().Root.Children);

        Assert.Equal(GuiTextInputCommitMode.Debounced, textField.Payload.TextCommitMode);
        Assert.Equal(TimeSpan.FromMilliseconds(180), textField.Payload.TextCommitDelay);
    }

    [Fact]
    public void Build_preserves_scroll_and_validation_message_nodes()
    {
        var builder = new GuiFrameBuilder("ui.style");

        using (builder.Scroll("details"))
        {
            builder.ValidationMessage(
                "warning",
                "Shader metadata is missing.",
                EditorDiagnosticSeverity.Warning);
        }

        var scroll = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Scroll, scroll.Kind);
        Assert.Equal("details", scroll.Id.KeyPath);

        var message = Assert.Single(scroll.Children);
        Assert.Equal(GuiNodeKind.ValidationMessage, message.Kind);
        Assert.Equal("details/warning", message.Id.KeyPath);
        Assert.Equal("Shader metadata is missing.", message.Label);
        Assert.Equal(EditorDiagnosticSeverity.Warning, message.Payload.DiagnosticSeverity);
    }

    [Fact]
    public void Build_preserves_foldout_payload_and_children()
    {
        var builder = new GuiFrameBuilder("ui.style");

        using (builder.Foldout("advanced", "Advanced", isExpanded: false))
        {
            builder.Label("hint", "Deferred details");
        }

        var foldout = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Foldout, foldout.Kind);
        Assert.Equal("advanced", foldout.Id.KeyPath);
        Assert.Equal("Advanced", foldout.Label);
        Assert.False(foldout.Payload.IsExpanded);

        var child = Assert.Single(foldout.Children);
        Assert.Equal("advanced/hint", child.Id.KeyPath);
        Assert.Equal("Deferred details", child.Label);
    }
}

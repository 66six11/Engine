using Editor.Core.CodeFirstUI;
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

        builder.TextField("filter", "Filter", "gbuffer");
        builder.Toggle("show-disabled", "Show Disabled", isChecked: true);

        var tree = builder.Build();

        var textField = tree.Root.Children[0];
        Assert.Equal(GuiNodeKind.TextField, textField.Kind);
        Assert.Equal("Filter", textField.Label);
        Assert.Equal("gbuffer", textField.Payload.TextValue);

        var toggle = tree.Root.Children[1];
        Assert.Equal(GuiNodeKind.Toggle, toggle.Kind);
        Assert.Equal("Show Disabled", toggle.Label);
        Assert.True(toggle.Payload.IsChecked);
    }
}

using System.Linq;
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
    public void Validate_reports_duplicate_sibling_keys()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        using (builder.Toolbar("toolbar"))
        {
            builder.Button("capture", "Capture Frame");
            builder.Button("capture", "Capture Again");
        }

        var result = new GuiTreeValidator().Validate(builder.Build());

        var error = Assert.Single(result.Errors);
        Assert.False(result.IsValid);
        Assert.Equal(GuiTreeValidationErrorCode.DuplicateKey, error.Code);
        Assert.Equal("render.frameDebugger/toolbar/capture", error.NodePath);
    }

    [Fact]
    public void Validate_allows_same_local_key_under_different_parents()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        using (builder.Vertical("left"))
        {
            builder.Button("capture", "Capture Frame");
        }

        using (builder.Vertical("right"))
        {
            builder.Button("capture", "Capture Frame");
        }

        var tree = builder.Build();
        var result = new GuiTreeValidator().Validate(tree);

        Assert.True(result.IsValid);
        Assert.Empty(result.Errors);
        Assert.Equal(
            ["left/capture", "right/capture"],
            tree.Root
                .Children
                .SelectMany(child => child.Children)
                .Select(child => child.Id.KeyPath)
                .ToArray());
    }
}

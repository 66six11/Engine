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
}

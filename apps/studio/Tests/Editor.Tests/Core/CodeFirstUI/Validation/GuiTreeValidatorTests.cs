using System.Linq;
using Editor.Core.CodeFirstUI;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI;

public sealed class GuiTreeValidatorTests
{
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

    [Fact]
    public void Validate_rejects_virtualized_list_inside_scroll()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        using (builder.Scroll("details"))
        {
            builder.List(
                "passes",
                [new GuiListItem("gbuffer", "GBuffer")],
                "gbuffer");
        }

        var result = new GuiTreeValidator().Validate(builder.Build());

        var error = Assert.Single(result.Errors);
        Assert.False(result.IsValid);
        Assert.Equal(GuiTreeValidationErrorCode.VirtualizedContentInsideScroll, error.Code);
        Assert.Equal("render.frameDebugger/details/passes", error.NodePath);
    }
}

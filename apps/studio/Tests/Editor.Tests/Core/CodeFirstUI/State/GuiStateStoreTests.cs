using Editor.Core.CodeFirstUI;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI;

public sealed class GuiStateStoreTests
{
    [Fact]
    public void State_store_preserves_text_and_selected_item_by_node_identity()
    {
        var store = new GuiStateStore();
        var filter = new GuiNodeId(
            "render.frameDebugger",
            "filter",
            GuiNodeKind.TextField);
        var passes = new GuiNodeId(
            "render.frameDebugger",
            "passes",
            GuiNodeKind.List);

        store.SetText(filter, "gbuffer");
        store.SetSelectedItem(passes, "pass-12");
        store.SetSplitRatio(new GuiNodeId(
            "render.frameDebugger",
            "layout",
            GuiNodeKind.Split), 0.35d);

        Assert.True(store.TryGetText(filter, out var text));
        Assert.Equal("gbuffer", text);
        Assert.True(store.TryGetSelectedItem(passes, out var selectedItemId));
        Assert.Equal("pass-12", selectedItemId);
        Assert.True(store.TryGetSplitRatio(new GuiNodeId(
            "render.frameDebugger",
            "layout",
            GuiNodeKind.Split), out var splitRatio));
        Assert.Equal(0.35d, splitRatio);
    }

    [Fact]
    public void State_store_clear_removes_values_for_incompatible_node()
    {
        var store = new GuiStateStore();
        var filter = new GuiNodeId(
            "render.frameDebugger",
            "filter",
            GuiNodeKind.TextField);
        var incompatibleFilter = filter with { Kind = GuiNodeKind.List };

        store.SetText(filter, "gbuffer");
        store.SetSplitRatio(filter with { Kind = GuiNodeKind.Split }, 0.5d);
        store.ClearNodeState(filter);
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Split });

        Assert.False(store.TryGetText(filter, out _));
        Assert.False(store.TryGetText(incompatibleFilter, out _));
        Assert.False(store.TryGetSplitRatio(filter with { Kind = GuiNodeKind.Split }, out _));
    }
}

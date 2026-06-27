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

        Assert.True(store.TryGetText(filter, out var text));
        Assert.Equal("gbuffer", text);
        Assert.True(store.TryGetSelectedItem(passes, out var selectedItemId));
        Assert.Equal("pass-12", selectedItemId);
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
        store.ClearNodeState(filter);

        Assert.False(store.TryGetText(filter, out _));
        Assert.False(store.TryGetText(incompatibleFilter, out _));
    }
}

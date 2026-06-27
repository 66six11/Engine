using Editor.Core.CodeFirstUI;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI;

public sealed class GuiEventAndStateTests
{
    [Fact]
    public void Button_click_event_is_consumed_once_for_matching_node()
    {
        var queue = new GuiEventQueue();
        var capture = new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button);

        queue.EnqueueButtonClicked(capture);

        Assert.True(queue.ConsumeButtonClicked(capture));
        Assert.False(queue.ConsumeButtonClicked(capture));
    }

    [Fact]
    public void Button_click_event_does_not_match_different_key_or_kind()
    {
        var queue = new GuiEventQueue();
        var capture = new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button);
        var textFieldWithSameKey = capture with { Kind = GuiNodeKind.TextField };
        var otherButton = capture with { KeyPath = "footer/capture" };

        queue.EnqueueButtonClicked(capture);

        Assert.False(queue.ConsumeButtonClicked(textFieldWithSameKey));
        Assert.False(queue.ConsumeButtonClicked(otherButton));
        Assert.True(queue.ConsumeButtonClicked(capture));
    }

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

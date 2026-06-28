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
        var toggle = new GuiNodeId(
            "render.frameDebugger",
            "show-disabled",
            GuiNodeKind.Toggle);
        var foldout = new GuiNodeId(
            "render.frameDebugger",
            "advanced",
            GuiNodeKind.Foldout);
        var navigation = new GuiNodeId(
            "render.frameDebugger",
            "catalog",
            GuiNodeKind.NavigationView);
        var slider = new GuiNodeId(
            "render.frameDebugger",
            "exposure",
            GuiNodeKind.Slider);

        store.SetText(filter, "gbuffer");
        store.SetSelectedItem(passes, "pass-12");
        store.SetToggle(toggle, isChecked: true);
        store.SetFoldoutExpanded(foldout, isExpanded: false);
        store.SetNumericValue(slider, 1.25d);
        store.SetNavigationRouteExpanded(navigation, "overview", isExpanded: false);
        store.SetSplitRatio(new GuiNodeId(
            "render.frameDebugger",
            "layout",
            GuiNodeKind.Split), 0.35d);

        Assert.True(store.TryGetText(filter, out var text));
        Assert.Equal("gbuffer", text);
        Assert.True(store.TryGetSelectedItem(passes, out var selectedItemId));
        Assert.Equal("pass-12", selectedItemId);
        Assert.True(store.TryGetToggle(toggle, out var isChecked));
        Assert.True(isChecked);
        Assert.True(store.TryGetFoldoutExpanded(foldout, out var isExpanded));
        Assert.False(isExpanded);
        Assert.True(store.TryGetNumericValue(slider, out var numericValue));
        Assert.Equal(1.25d, numericValue);
        Assert.True(store.TryGetNavigationRouteExpanded(navigation, "overview", out var routeExpanded));
        Assert.False(routeExpanded);
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
        var toggle = filter with { Kind = GuiNodeKind.Toggle };

        store.SetText(filter, "gbuffer");
        store.SetToggle(toggle, isChecked: true);
        store.SetFoldoutExpanded(filter with { Kind = GuiNodeKind.Foldout }, isExpanded: false);
        store.SetNumericValue(filter with { Kind = GuiNodeKind.Slider }, 1.25d);
        store.SetNavigationRouteExpanded(filter with { Kind = GuiNodeKind.NavigationView }, "overview", isExpanded: false);
        store.SetSplitRatio(filter with { Kind = GuiNodeKind.Split }, 0.5d);
        store.ClearNodeState(filter);
        store.ClearNodeState(toggle);
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Foldout });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Slider });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.NavigationView });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Split });

        Assert.False(store.TryGetText(filter, out _));
        Assert.False(store.TryGetText(incompatibleFilter, out _));
        Assert.False(store.TryGetToggle(toggle, out _));
        Assert.False(store.TryGetFoldoutExpanded(filter with { Kind = GuiNodeKind.Foldout }, out _));
        Assert.False(store.TryGetNumericValue(filter with { Kind = GuiNodeKind.Slider }, out _));
        Assert.False(store.TryGetNavigationRouteExpanded(filter with { Kind = GuiNodeKind.NavigationView }, "overview", out _));
        Assert.False(store.TryGetSplitRatio(filter with { Kind = GuiNodeKind.Split }, out _));
    }
}

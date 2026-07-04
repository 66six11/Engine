using Editor.Core.CodeFirstUI.Models;
using Editor.Core.CodeFirstUI.State;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI.State;

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
        var colorField = new GuiNodeId(
            "render.frameDebugger",
            "albedo",
            GuiNodeKind.ColorField);
        var color = new GuiColorValue(255, 128, 64, 192);
        var vector3Field = new GuiNodeId(
            "render.frameDebugger",
            "position",
            GuiNodeKind.Vector3Field);
        var position = new GuiVector3Value(1d, 2d, 3d);
        var vector2Field = new GuiNodeId(
            "render.frameDebugger",
            "uv-scale",
            GuiNodeKind.Vector2Field);
        var uvScale = new GuiVector2Value(2d, 4d);
        var vector4Field = new GuiNodeId(
            "render.frameDebugger",
            "tiling-offset",
            GuiNodeKind.Vector4Field);
        var tilingOffset = new GuiVector4Value(1d, 2d, 3d, 4d);

        store.SetText(filter, "gbuffer");
        store.SetSelectedItem(passes, "pass-12");
        store.SetToggle(toggle, isChecked: true);
        store.SetFoldoutExpanded(foldout, isExpanded: false);
        store.SetNumericValue(slider, 1.25d);
        store.SetColorValue(colorField, color);
        store.SetVector3Value(vector3Field, position);
        store.SetVector2Value(vector2Field, uvScale);
        store.SetVector4Value(vector4Field, tilingOffset);
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
        Assert.True(store.TryGetColorValue(colorField, out var colorValue));
        Assert.Equal(color, colorValue);
        Assert.True(store.TryGetVector3Value(vector3Field, out var vector3Value));
        Assert.Equal(position, vector3Value);
        Assert.True(store.TryGetVector2Value(vector2Field, out var vector2Value));
        Assert.Equal(uvScale, vector2Value);
        Assert.True(store.TryGetVector4Value(vector4Field, out var vector4Value));
        Assert.Equal(tilingOffset, vector4Value);
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
        store.SetColorValue(filter with { Kind = GuiNodeKind.ColorField }, new GuiColorValue(255, 128, 64, 192));
        store.SetVector3Value(filter with { Kind = GuiNodeKind.Vector3Field }, new GuiVector3Value(1d, 2d, 3d));
        store.SetVector2Value(filter with { Kind = GuiNodeKind.Vector2Field }, new GuiVector2Value(2d, 4d));
        store.SetVector4Value(filter with { Kind = GuiNodeKind.Vector4Field }, new GuiVector4Value(1d, 2d, 3d, 4d));
        store.SetNavigationRouteExpanded(filter with { Kind = GuiNodeKind.NavigationView }, "overview", isExpanded: false);
        store.SetSplitRatio(filter with { Kind = GuiNodeKind.Split }, 0.5d);
        store.ClearNodeState(filter);
        store.ClearNodeState(toggle);
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Foldout });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Slider });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.ColorField });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Vector3Field });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Vector2Field });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Vector4Field });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.NavigationView });
        store.ClearNodeState(filter with { Kind = GuiNodeKind.Split });

        Assert.False(store.TryGetText(filter, out _));
        Assert.False(store.TryGetText(incompatibleFilter, out _));
        Assert.False(store.TryGetToggle(toggle, out _));
        Assert.False(store.TryGetFoldoutExpanded(filter with { Kind = GuiNodeKind.Foldout }, out _));
        Assert.False(store.TryGetNumericValue(filter with { Kind = GuiNodeKind.Slider }, out _));
        Assert.False(store.TryGetColorValue(filter with { Kind = GuiNodeKind.ColorField }, out _));
        Assert.False(store.TryGetVector3Value(filter with { Kind = GuiNodeKind.Vector3Field }, out _));
        Assert.False(store.TryGetVector2Value(filter with { Kind = GuiNodeKind.Vector2Field }, out _));
        Assert.False(store.TryGetVector4Value(filter with { Kind = GuiNodeKind.Vector4Field }, out _));
        Assert.False(store.TryGetNavigationRouteExpanded(filter with { Kind = GuiNodeKind.NavigationView }, "overview", out _));
        Assert.False(store.TryGetSplitRatio(filter with { Kind = GuiNodeKind.Split }, out _));
    }
}

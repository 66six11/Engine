using System;
using System.Collections.Generic;
using Editor.UI.Controls.Tree;
using Xunit;

namespace Editor.Tests.UI.Controls.Tree;

public sealed class EditorTreeModelTests
{
    [Fact]
    public void Flattener_hides_descendants_of_collapsed_nodes_and_preserves_input_order()
    {
        var nodes = CreateSceneNodes();
        var expansion = new EditorTreeExpansionState(["scene:test"]);

        var rows = EditorTreeFlattener.Flatten(nodes, expansion);

        Assert.Equal(["scene:test", "scene:test/cube", "scene:test/light"], GetRowIds(rows));
        Assert.True(rows[0].IsExpanded);
        Assert.False(rows[1].IsExpanded);
        Assert.False(rows[1].IsLastSibling);
        Assert.True(rows[2].IsLastSibling);
    }

    [Fact]
    public void Expansion_state_toggles_only_nodes_that_have_children()
    {
        var expansion = new EditorTreeExpansionState(["scene:test"]);

        Assert.True(expansion.Toggle("scene:test", hasChildren: true));
        Assert.False(expansion.IsExpanded("scene:test"));

        Assert.False(expansion.Toggle("scene:test/light", hasChildren: false));
        Assert.False(expansion.IsExpanded("scene:test/light"));
    }

    [Fact]
    public void Filter_keeps_matching_ancestors_visible_without_mutating_expansion_state()
    {
        var nodes = CreateSceneNodes();
        var expansion = new EditorTreeExpansionState();

        var rows = EditorTreeFlattener.Flatten(
            nodes,
            expansion,
            node => node.Payload.Contains("renderer", StringComparison.OrdinalIgnoreCase));

        Assert.Equal(["scene:test", "scene:test/cube", "scene:test/cube/renderer"], GetRowIds(rows));
        Assert.True(rows[0].IsExpanded);
        Assert.True(rows[1].IsExpanded);
        Assert.True(rows[2].IsSearchMatch);
        Assert.False(expansion.IsExpanded("scene:test"));
        Assert.False(expansion.IsExpanded("scene:test/cube"));
    }

    [Fact]
    public void Flattener_reports_branch_continuation_metadata_for_custom_rows()
    {
        var nodes = CreateSceneNodes();
        var expansion = new EditorTreeExpansionState(["scene:test", "scene:test/cube"]);

        var rows = EditorTreeFlattener.Flatten(nodes, expansion);

        Assert.Equal(["scene:test", "scene:test/cube", "scene:test/cube/renderer", "scene:test/light"], GetRowIds(rows));
        Assert.Equal(1, rows[1].Depth);
        Assert.False(rows[1].IsLastSibling);
        Assert.Equal(2, rows[2].Depth);
        Assert.True(rows[2].IsLastSibling);
        Assert.Equal(1UL, rows[2].AncestorContinuationMask);
        Assert.Equal("Mesh Renderer", rows[2].Payload);
    }

    private static IReadOnlyList<EditorTreeNode<string>> CreateSceneNodes()
    {
        return
        [
            new("scene:test", null, "Scene"),
            new("scene:test/cube", "scene:test", "Cube"),
            new("scene:test/cube/renderer", "scene:test/cube", "Mesh Renderer"),
            new("scene:test/light", "scene:test", "Light"),
        ];
    }

    private static string[] GetRowIds(IReadOnlyList<EditorTreeRow<string>> rows)
    {
        var ids = new string[rows.Count];
        for (var index = 0; index < rows.Count; index++)
        {
            ids[index] = rows[index].Id;
        }

        return ids;
    }
}

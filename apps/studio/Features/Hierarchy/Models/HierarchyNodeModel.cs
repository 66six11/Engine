using Editor.Core.Models;

namespace Editor.Features.Hierarchy.Models;

public sealed record HierarchyNodeModel(
    string Id,
    string DisplayName,
    string Kind,
    string? IconKey = null,
    string? ParentId = null)
{
    public EditorSelectionItem ToSelectionItem()
    {
        return new EditorSelectionItem(Id, Kind, DisplayName, IconKey);
    }
}

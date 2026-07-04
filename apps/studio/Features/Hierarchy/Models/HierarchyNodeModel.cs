using Editor.Core.Models.Scene;
using Editor.Core.Models.Selection;

namespace Editor.Features.Hierarchy.Models;

public sealed record HierarchyNodeModel(
    string Id,
    string DisplayName,
    string Kind,
    string? IconKey = null,
    string? ParentId = null)
{
    public static HierarchyNodeModel FromSceneObject(SceneObjectSnapshot sceneObject)
    {
        return new HierarchyNodeModel(
            sceneObject.Id,
            sceneObject.DisplayName,
            sceneObject.Kind,
            sceneObject.IconKey,
            sceneObject.ParentId);
    }

    public EditorSelectionItem ToSelectionItem()
    {
        return new EditorSelectionItem(Id, Kind, DisplayName, IconKey);
    }
}

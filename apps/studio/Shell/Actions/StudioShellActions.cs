using Asharia.Studio.Application.Actions;

namespace Editor.Shell.Actions;

internal static class StudioShellActionIds
{
    public static StudioActionId CreateProject { get; } =
        new("studio.file.create-project");
    public static StudioActionId OpenProject { get; } =
        new("studio.file.open-project");
    public static StudioActionId CloseProject { get; } =
        new("studio.file.close-project");
    public static StudioActionId SaveScene { get; } =
        new("studio.file.save-scene");
    public static StudioActionId UndoScene { get; } =
        new("studio.edit.undo-scene");
    public static StudioActionId RedoScene { get; } =
        new("studio.edit.redo-scene");
    public static StudioActionId CreateEntity { get; } =
        new("studio.scene.create-entity");
    public static StudioActionId CreateMeshEntity { get; } =
        new("studio.scene.create-mesh-entity");
    public static StudioActionId ApplyEntityName { get; } =
        new("studio.scene.apply-entity-name");
    public static StudioActionId ApplyEntityTransform { get; } =
        new("studio.scene.apply-entity-transform");
    public static StudioActionId OpenHierarchyPanel { get; } =
        new("studio.window.open-hierarchy-panel");
    public static StudioActionId OpenProjectPanel { get; } =
        new("studio.window.open-project-panel");
    public static StudioActionId OpenSceneViewPanel { get; } =
        new("studio.window.open-scene-view-panel");
    public static StudioActionId OpenInspectorPanel { get; } =
        new("studio.window.open-inspector-panel");
}

internal static class StudioShellPresentationIds
{
    public static StudioPresentationId MainWindow { get; } =
        new("main-window");
}

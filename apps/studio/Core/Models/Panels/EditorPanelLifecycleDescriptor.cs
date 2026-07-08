namespace Editor.Core.Models.Panels;

public sealed record EditorPanelLifecycleDescriptor(EditorPanelLifecycleMode Mode)
{
    public static EditorPanelLifecycleDescriptor None { get; } =
        new(EditorPanelLifecycleMode.None);

    public static EditorPanelLifecycleDescriptor ContentObject { get; } =
        new(EditorPanelLifecycleMode.ContentObject);
}

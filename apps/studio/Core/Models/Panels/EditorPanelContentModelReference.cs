namespace Editor.Core.Models.Panels;

public sealed record EditorPanelContentModelReference(
    EditorPanelContentModelKind Kind,
    string ModelId);

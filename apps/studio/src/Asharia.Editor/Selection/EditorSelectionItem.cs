namespace Asharia.Editor.Selection;

public sealed record EditorSelectionItem(
    string Id,
    string Kind,
    string DisplayName,
    string? IconKey = null);

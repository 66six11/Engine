using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorSelectionSnapshot(
    string? ActiveContextId,
    IReadOnlyList<EditorSelectionItem> Items)
{
    public static EditorSelectionSnapshot Empty { get; } = new(null, []);

    public bool HasSelection => Items.Count > 0;

    public EditorSelectionItem? PrimaryItem => HasSelection ? Items[0] : null;
}

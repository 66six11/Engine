using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI;

public sealed record GuiNodePayload
{
    public static GuiNodePayload None { get; } = new();

    public GuiSplitDirection? SplitDirection { get; init; }

    public double? SplitRatio { get; init; }

    public IReadOnlyList<GuiListItem> ListItems { get; init; } = [];

    public string? SelectedItemId { get; init; }

    public string? TextValue { get; init; }

    public bool? IsChecked { get; init; }
}

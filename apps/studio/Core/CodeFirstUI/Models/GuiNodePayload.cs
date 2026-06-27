using System;
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

    public GuiTextInputCommitMode? TextCommitMode { get; init; }

    public TimeSpan? TextCommitDelay { get; init; }

    public bool? IsChecked { get; init; }
}

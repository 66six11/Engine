using System;
using System.Collections.Generic;
using Editor.Core.Models;

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

    public GuiTextTone? TextTone { get; init; }

    public GuiTextSize? TextSize { get; init; }

    public bool? IsChecked { get; init; }

    public bool? IsExpanded { get; init; }

    public EditorDiagnosticSeverity? DiagnosticSeverity { get; init; }
}

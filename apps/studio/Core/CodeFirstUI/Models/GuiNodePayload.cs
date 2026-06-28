using System;
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.CodeFirstUI;

public sealed record GuiNodePayload
{
    public static GuiNodePayload None { get; } = new();

    public GuiSplitDirection? SplitDirection { get; init; }

    public double? SplitRatio { get; init; }

    public IReadOnlyList<GuiNavigationItem> NavigationItems { get; init; } = [];

    public string? SelectedRoute { get; init; }

    public IReadOnlyList<string> CollapsedNavigationRoutes { get; init; } = [];

    public IReadOnlyList<GuiListItem> ListItems { get; init; } = [];

    public string? SelectedItemId { get; init; }

    public string? TextValue { get; init; }

    public GuiTextInputCommitMode? TextCommitMode { get; init; }

    public TimeSpan? TextCommitDelay { get; init; }

    public GuiTextTone? TextTone { get; init; }

    public GuiTextSize? TextSize { get; init; }

    public bool? IsChecked { get; init; }

    public bool? IsExpanded { get; init; }

    public double? NumericValue { get; init; }

    public double? NumericMinimum { get; init; }

    public double? NumericMaximum { get; init; }

    public double? NumericSmallChange { get; init; }

    public double? NumericLargeChange { get; init; }

    public string? NumericFormatString { get; init; }

    public GuiColorValue? ColorValue { get; init; }

    public bool? ShowAlpha { get; init; }

    public GuiVector2Value? Vector2Value { get; init; }

    public GuiVector3Value? Vector3Value { get; init; }

    public GuiVector4Value? Vector4Value { get; init; }

    public bool? IsIndeterminate { get; init; }

    public bool? ShowProgressText { get; init; }

    public string? ProgressTextFormat { get; init; }

    public EditorDiagnosticSeverity? DiagnosticSeverity { get; init; }
}

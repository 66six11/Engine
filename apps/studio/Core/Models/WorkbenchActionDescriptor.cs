namespace Editor.Core.Models;

public sealed record WorkbenchActionDescriptor(
    string Id,
    string Title,
    WorkbenchActionKind Kind,
    string MenuPath,
    string? TargetId = null,
    string? IconKey = null,
    string Category = "General",
    string? DefaultShortcut = null,
    WorkbenchActionScope Scope = WorkbenchActionScope.Global,
    bool IsEnabled = true,
    string? DisabledReason = null,
    string? SearchText = null);

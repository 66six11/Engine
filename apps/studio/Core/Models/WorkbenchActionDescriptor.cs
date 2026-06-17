namespace Editor.Core.Models;

public sealed record WorkbenchActionDescriptor(
    string Id,
    string Title,
    WorkbenchActionKind Kind,
    string MenuPath,
    string? TargetId = null,
    string? IconKey = null);

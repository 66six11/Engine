namespace Editor.Core.Models;

public sealed record EditorActionContributionDescriptor(
    string Id,
    string Title,
    string Category,
    WorkbenchActionScope Scope,
    string? DefaultShortcut,
    string MenuPath,
    string CommandId);

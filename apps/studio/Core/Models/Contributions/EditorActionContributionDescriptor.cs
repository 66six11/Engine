using Editor.Core.Models.Workbench;

namespace Editor.Core.Models.Contributions;

public sealed record EditorActionContributionDescriptor(
    string Id,
    string Title,
    string Category,
    WorkbenchActionScope Scope,
    string? DefaultShortcut,
    string MenuPath,
    string CommandId);

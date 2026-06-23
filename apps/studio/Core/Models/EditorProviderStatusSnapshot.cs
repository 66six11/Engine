namespace Editor.Core.Models;

public sealed record EditorProviderStatusSnapshot(
    string Id,
    string Role,
    EditorExtensionId OwnerId,
    EditorProviderState State,
    string? Message = null);

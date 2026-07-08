using Editor.Core.Models.Extensions;

namespace Editor.Core.Models.Scene;

public sealed record EditorProviderStatusSnapshot(
    string Id,
    string Role,
    EditorExtensionId OwnerId,
    EditorProviderState State,
    string? Message = null);

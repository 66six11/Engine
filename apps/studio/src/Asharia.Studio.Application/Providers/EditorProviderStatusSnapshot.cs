using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Providers;

public enum EditorProviderState
{
    Created,
    Ready,
    Faulted,
}

public sealed record EditorProviderStatusSnapshot(
    string Id,
    string Role,
    EditorModuleDefinitionId OwnerId,
    EditorProviderState State,
    string? Message = null);

using Editor.Core.Abstractions;

namespace Editor.Shell.Composition;

internal sealed class EditorExtensionActivationContext : IEditorExtensionActivationContext
{
    public static EditorExtensionActivationContext Instance { get; } = new();

    private EditorExtensionActivationContext()
    {
    }
}

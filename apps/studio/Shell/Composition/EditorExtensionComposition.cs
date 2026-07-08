using Editor.Core.Abstractions;

namespace Editor.Shell.Composition;

internal sealed record EditorExtensionComposition(
    IPanelRegistry PanelRegistry,
    IWorkbenchActionRegistry ActionRegistry,
    EditorProviderHost ProviderHost);

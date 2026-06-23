using Editor.Core.Abstractions;
using Editor.Shell.Docking;
using Editor.Shell.Selection;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionRoot
{
    public StudioCompositionSession CreateMainWindowSession()
    {
        return CreateMainWindowSession(EditorDockLayoutStore.TryLoad());
    }

    internal StudioCompositionSession CreateMainWindowSession(EditorDockLayoutSnapshot? savedLayout)
    {
        var selectionService = new EditorSelectionService();
        var host = CreateDefaultHost(selectionService);
        var composition = host.Compose();
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout,
            selectionService);
        return new StudioCompositionSession(viewModel, composition, host);
    }

    public static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();
        return CreateDefaultHost(selectionService).Compose();
    }

    private static EditorExtensionHost CreateDefaultHost(
        IEditorSelectionService selectionService)
    {
        return new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules(selectionService));
    }
}

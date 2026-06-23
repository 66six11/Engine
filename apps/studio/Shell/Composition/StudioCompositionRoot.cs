using Editor.Core.Abstractions;
using Editor.Shell.Docking;
using Editor.Shell.Selection;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionRoot
{
    public MainWindowViewModel CreateMainWindowViewModel()
    {
        return CreateMainWindowViewModel(EditorDockLayoutStore.TryLoad());
    }

    internal MainWindowViewModel CreateMainWindowViewModel(EditorDockLayoutSnapshot? savedLayout)
    {
        var selectionService = new EditorSelectionService();
        var composition = CreateDefaultComposition(selectionService);
        return new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout,
            selectionService);
    }

    public static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();
        var host = new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules(selectionService));
        return host.Compose();
    }
}

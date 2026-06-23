using Editor.Core.Abstractions;
using Editor.Core.Services;
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
        var diagnostics = new EditorDiagnosticService();
        var host = CreateDefaultHost(selectionService, diagnostics);
        var composition = host.Compose();
        var viewModel = new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout,
            selectionService,
            diagnostics: diagnostics);
        return new StudioCompositionSession(viewModel, composition, host);
    }

    public static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();
        return CreateDefaultHost(selectionService, diagnostics).Compose();
    }

    private static EditorExtensionHost CreateDefaultHost(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        return new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics));
    }
}

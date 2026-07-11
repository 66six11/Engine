using System;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Selection;
using Editor.Core.Services;
using Editor.Shell.Compatibility;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Selection;
using Editor.Shell.ViewModels.Windowing;

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
        return CreateMainWindowSession(
            savedLayout,
            new LegacyEditorModuleCompatibilityAdapter(
                EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics)),
            selectionService,
            diagnostics);
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        LegacyEditorModuleCompatibilityAdapter modules)
    {
        return CreateMainWindowSession(
            savedLayout,
            modules,
            new EditorSelectionService(),
            new EditorDiagnosticService());
    }

    private static StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        LegacyEditorModuleCompatibilityAdapter modules,
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        var compatibilityAdapter = modules;
        var composition = compatibilityAdapter.Compose();
        try
        {
            compatibilityAdapter.ActivateAsync().GetAwaiter().GetResult();
            var viewModel = new MainWindowViewModel(
                composition.PanelRegistry,
                composition.ActionRegistry,
                savedLayout,
                selectionService,
                diagnostics: diagnostics);
            return new StudioCompositionSession(viewModel, composition, compatibilityAdapter);
        }
        catch (Exception exception)
        {
            DisposeAdapterAfterCreationFailure(compatibilityAdapter, exception);
            throw;
        }
    }

    private static void DisposeAdapterAfterCreationFailure(
        LegacyEditorModuleCompatibilityAdapter compatibilityAdapter,
        Exception creationException)
    {
        try
        {
            compatibilityAdapter.DisposeAsync().GetAwaiter().GetResult();
        }
        catch (Exception disposeException)
        {
            throw new AggregateException(creationException, disposeException);
        }
    }

    public static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();
        return CreateDefaultCompatibilityAdapter(selectionService, diagnostics).Compose();
    }

    private static LegacyEditorModuleCompatibilityAdapter CreateDefaultCompatibilityAdapter(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        return new LegacyEditorModuleCompatibilityAdapter(
            EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics));
    }
}

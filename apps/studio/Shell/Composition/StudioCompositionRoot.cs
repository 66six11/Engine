using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Services;
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
            EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics),
            selectionService,
            diagnostics);
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        IEnumerable<IEditorExtensionModule> modules)
    {
        return CreateMainWindowSession(
            savedLayout,
            modules,
            new EditorSelectionService(),
            new EditorDiagnosticService());
    }

    private static StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        IEnumerable<IEditorExtensionModule> modules,
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        var host = new EditorExtensionHost(modules);
        var composition = host.Compose();
        try
        {
            host.ActivateAsync().GetAwaiter().GetResult();
            var viewModel = new MainWindowViewModel(
                composition.PanelRegistry,
                composition.ActionRegistry,
                savedLayout,
                selectionService,
                diagnostics: diagnostics);
            return new StudioCompositionSession(viewModel, composition, host);
        }
        catch (Exception exception)
        {
            DisposeHostAfterCreationFailure(host, exception);
            throw;
        }
    }

    private static void DisposeHostAfterCreationFailure(
        EditorExtensionHost host,
        Exception creationException)
    {
        try
        {
            host.DisposeAsync().GetAwaiter().GetResult();
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
        return CreateDefaultHost(selectionService, diagnostics).Compose();
    }

    private static EditorExtensionHost CreateDefaultHost(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        return new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics));
    }
}

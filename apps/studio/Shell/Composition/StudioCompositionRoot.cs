using System;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Projects;
using Asharia.Editor.Selection;
using Editor.Core.Services;
using Editor.Shell.Compatibility;
using Editor.Shell.Docking.Layout;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Projects;
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
        return CreateMainWindowSession(
            savedLayout,
            new ProjectOpenSessionSnapshotSource());
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        IProjectOpenSessionSnapshotSource projectOpenSessions)
    {
        ArgumentNullException.ThrowIfNull(projectOpenSessions);

        var selectionService = new EditorSelectionService();
        var diagnostics = new EditorDiagnosticService();
        return CreateMainWindowSession(
            savedLayout,
            new LegacyEditorModuleCompatibilityAdapter(
                EditorFeatureCatalog.CreateDefaultModules(
                    selectionService,
                    diagnostics,
                    projectOpenSessions)),
            selectionService,
            diagnostics,
            projectOpenSessions);
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        LegacyEditorModuleCompatibilityAdapter modules)
    {
        return CreateMainWindowSession(
            savedLayout,
            modules,
            new EditorSelectionService(),
            new EditorDiagnosticService(),
            new ProjectOpenSessionSnapshotSource());
    }

    private static StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        LegacyEditorModuleCompatibilityAdapter modules,
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics,
        IProjectOpenSessionSnapshotSource projectOpenSessions)
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
                diagnostics: diagnostics,
                projectOpenSessions: projectOpenSessions,
                defaultLayoutFactory: EditorWorkbenchLayoutPreset.CreateDefault);
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
        IEditorDiagnosticService? diagnostics = null,
        IProjectOpenSessionSnapshotSource? projectOpenSessions = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();
        projectOpenSessions ??= new ProjectOpenSessionSnapshotSource();
        return CreateDefaultCompatibilityAdapter(
            selectionService,
            diagnostics,
            projectOpenSessions).Compose();
    }

    private static LegacyEditorModuleCompatibilityAdapter CreateDefaultCompatibilityAdapter(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics,
        IProjectOpenSessionSnapshotSource projectOpenSessions)
    {
        return new LegacyEditorModuleCompatibilityAdapter(
            EditorFeatureCatalog.CreateDefaultModules(
                selectionService,
                diagnostics,
                projectOpenSessions));
    }
}

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
using Editor.Core.Interop.Projects.Adapters;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionRoot
{
    public StudioCompositionSession CreateMainWindowSession()
    {
        var projectSessions = CreateProjectSessionService();
        _ = projectSessions.RestoreRecentProject();
        return CreateMainWindowSession(
            EditorDockLayoutStore.TryLoad(),
            new ProjectOpenSessionSnapshotSource(),
            projectSessions);
    }

    internal StudioCompositionSession CreateMainWindowSession(EditorDockLayoutSnapshot? savedLayout)
    {
        return CreateMainWindowSession(
            savedLayout,
            new ProjectOpenSessionSnapshotSource(),
            CreateProjectSessionService());
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        IProjectOpenSessionSnapshotSource projectOpenSessions)
    {
        return CreateMainWindowSession(
            savedLayout,
            projectOpenSessions,
            CreateProjectSessionService());
    }

    internal StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        IProjectOpenSessionSnapshotSource projectOpenSessions,
        IProjectSessionService projectSessions)
    {
        ArgumentNullException.ThrowIfNull(projectOpenSessions);
        ArgumentNullException.ThrowIfNull(projectSessions);

        var selectionService = new EditorSelectionService();
        var diagnostics = new EditorDiagnosticService();
        return CreateMainWindowSession(
            savedLayout,
            new LegacyEditorModuleCompatibilityAdapter(
                EditorFeatureCatalog.CreateDefaultModules(
                    selectionService,
                    diagnostics)),
            selectionService,
            diagnostics,
            projectOpenSessions,
            projectSessions);
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
            new ProjectOpenSessionSnapshotSource(),
            CreateProjectSessionService());
    }

    private static StudioCompositionSession CreateMainWindowSession(
        EditorDockLayoutSnapshot? savedLayout,
        LegacyEditorModuleCompatibilityAdapter modules,
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics,
        IProjectOpenSessionSnapshotSource projectOpenSessions,
        IProjectSessionService projectSessions)
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
                projectSessions: projectSessions,
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
        IEditorDiagnosticService? diagnostics = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();
        return CreateDefaultCompatibilityAdapter(
            selectionService,
            diagnostics).Compose();
    }

    private static LegacyEditorModuleCompatibilityAdapter CreateDefaultCompatibilityAdapter(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
    {
        return new LegacyEditorModuleCompatibilityAdapter(
            EditorFeatureCatalog.CreateDefaultModules(
                selectionService,
                diagnostics));
    }

    private static ProjectSessionService CreateProjectSessionService()
    {
        return new ProjectSessionService(new ProjectDescriptorGateway());
    }
}

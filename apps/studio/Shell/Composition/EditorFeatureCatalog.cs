using System.Collections.Generic;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Selection;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Abstractions;
using Editor.Core.Services;
using Editor.Features.Workbench;
using Asharia.Studio.Application.Selection;

namespace Editor.Shell.Composition;

internal static class EditorFeatureCatalog
{
    public static IReadOnlyList<IEditorFeatureModule> CreateDefaultModules(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null,
        ISceneSnapshotProvider? sceneSnapshotProvider = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();

        var workbench = sceneSnapshotProvider is null
            ? new WorkbenchFeatureModule(selectionService, diagnostics)
            : new WorkbenchFeatureModule(
                selectionService,
                diagnostics,
                sceneSnapshotProvider);
        return [workbench];
    }
}

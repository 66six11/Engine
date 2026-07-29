using System.Collections.Generic;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Projects;
using Asharia.Editor.Selection;
using Editor.Core.Abstractions;
using Editor.Core.Services;
using Editor.Features.Workbench;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Selection;

namespace Editor.Shell.Composition;

internal static class EditorFeatureCatalog
{
    public static IReadOnlyList<IEditorFeatureModule> CreateDefaultModules(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null,
        IProjectOpenSessionSnapshotSource? projectOpenSessions = null)
    {
        selectionService ??= new EditorSelectionService();
        diagnostics ??= new EditorDiagnosticService();
        projectOpenSessions ??= new ProjectOpenSessionSnapshotSource();

        return
        [
            new WorkbenchFeatureModule(
                selectionService,
                diagnostics,
                projectOpenSessions),
        ];
    }
}

using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Features.Workbench;
using Editor.Shell.Selection;

namespace Editor.Shell.Composition;

internal static class EditorFeatureCatalog
{
    public static IReadOnlyList<IEditorFeatureModule> CreateDefaultModules(
        IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();

        return
        [
            new WorkbenchFeatureModule(selectionService),
        ];
    }
}

using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Features.Workbench;

namespace Editor.Shell.Composition;

internal static class EditorFeatureCatalog
{
    public static IReadOnlyList<IEditorFeatureModule> CreateDefaultModules()
    {
        return
        [
            new WorkbenchFeatureModule(),
        ];
    }
}

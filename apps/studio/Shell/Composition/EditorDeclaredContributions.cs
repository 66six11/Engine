using System.Collections.Generic;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Scene;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Composition;

internal sealed record EditorDeclaredContributions(
    EditorExtensionId OwnerId,
    IReadOnlyList<PanelDescriptor> Panels,
    IReadOnlyList<WorkbenchActionDescriptor> Actions,
    IReadOnlyList<SceneProviderDescriptor> SceneProviders);

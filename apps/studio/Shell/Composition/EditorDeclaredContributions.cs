using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Shell.Composition;

internal sealed record EditorDeclaredContributions(
    EditorExtensionId OwnerId,
    IReadOnlyList<PanelDescriptor> Panels,
    IReadOnlyList<WorkbenchActionDescriptor> Actions,
    IReadOnlyList<SceneProviderDescriptor> SceneProviders);

using System;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorFeatureModule : IEditorExtensionModule
{
}

internal static class EditorFeatureModuleRegistrationExtensions
{
    public static void RegisterPanels(this IEditorFeatureModule module, IPanelRegistry panels)
    {
        ArgumentNullException.ThrowIfNull(module);
        ArgumentNullException.ThrowIfNull(panels);

        module.Declare(new RegistryProjectionContributionBuilder(panels, actions: null));
    }

    public static void RegisterActions(this IEditorFeatureModule module, IWorkbenchActionRegistry actions)
    {
        ArgumentNullException.ThrowIfNull(module);
        ArgumentNullException.ThrowIfNull(actions);

        module.Declare(new RegistryProjectionContributionBuilder(panels: null, actions));
    }

    private sealed class RegistryProjectionContributionBuilder(
        IPanelRegistry? panels,
        IWorkbenchActionRegistry? actions) : IEditorContributionBuilder
    {
        public void AddPanel(PanelDescriptor descriptor)
        {
            ArgumentNullException.ThrowIfNull(descriptor);
            panels?.Register(descriptor);
        }

        public void AddAction(WorkbenchActionDescriptor descriptor)
        {
            ArgumentNullException.ThrowIfNull(descriptor);
            actions?.Register(descriptor);
        }
    }
}

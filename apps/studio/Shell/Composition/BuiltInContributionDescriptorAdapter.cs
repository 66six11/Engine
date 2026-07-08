using System;
using Editor.Core.Models.Contributions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Composition;

internal sealed class BuiltInContributionDescriptorAdapter
{
    public EditorContributionDescriptorSet Adapt(EditorDeclaredContributions contributions)
    {
        ArgumentNullException.ThrowIfNull(contributions);

        var sourceId = contributions.OwnerId.Value;
        var panels = new EditorPanelContributionDescriptor[contributions.Panels.Count];
        var actions = new EditorActionContributionDescriptor[contributions.Actions.Count];

        for (var index = 0; index < panels.Length; index++)
        {
            panels[index] = AdaptPanel(sourceId, contributions.Panels[index]);
        }

        for (var index = 0; index < actions.Length; index++)
        {
            actions[index] = AdaptAction(contributions.Actions[index]);
        }

        return new EditorContributionDescriptorSet(
            new EditorContributionSourceId(sourceId),
            EditorContributionSourceKind.BuiltIn,
            panels,
            actions,
            DiagnosticSources: []);
    }

    private static EditorPanelContributionDescriptor AdaptPanel(
        string sourceId,
        PanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        return new EditorPanelContributionDescriptor(
            descriptor.Id,
            descriptor.Title,
            descriptor.Kind,
            descriptor.DefaultArea,
            descriptor.MenuPath,
            descriptor.CachePolicy,
            new EditorPanelContentModelReference(
                EditorPanelContentModelKind.ViewModelTypeReference,
                $"{sourceId}/{descriptor.Id}"),
            EditorPanelLifecycleDescriptor.ContentObject,
            EditorPanelFrameUpdateDescriptor.Manual);
    }

    private static EditorActionContributionDescriptor AdaptAction(
        WorkbenchActionDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        return new EditorActionContributionDescriptor(
            descriptor.Id,
            descriptor.Title,
            descriptor.Category,
            descriptor.Scope,
            descriptor.DefaultShortcut,
            descriptor.MenuPath,
            descriptor.Id);
    }
}

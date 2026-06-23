using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Composition;

internal sealed class EditorContributionBuilder : IEditorContributionBuilder
{
    private readonly List<PanelDescriptor> panels_ = [];
    private readonly List<WorkbenchActionDescriptor> actions_ = [];

    public void AddPanel(PanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        panels_.Add(descriptor);
    }

    public void AddAction(WorkbenchActionDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        actions_.Add(descriptor);
    }

    public EditorDeclaredContributions Build(EditorExtensionId ownerId)
    {
        return new EditorDeclaredContributions(
            ownerId,
            panels_.ToArray(),
            actions_.ToArray());
    }
}

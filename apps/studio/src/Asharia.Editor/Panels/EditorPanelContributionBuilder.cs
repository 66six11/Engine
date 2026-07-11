using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Panels;

public sealed class EditorPanelContributionBuilder
{
    private readonly EditorModuleBuilder owner_;

    internal EditorPanelContributionBuilder(EditorModuleBuilder owner)
    {
        owner_ = owner;
    }

    public void Add(EditorPanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        owner_.AddPanel(descriptor);
    }
}

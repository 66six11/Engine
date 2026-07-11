using System.Collections.Generic;
using Asharia.Editor.Contributions;
using Asharia.Editor.Extensions;
using Asharia.Editor.Panels;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorScopePartition
{
    internal EditorScopePartition(
        ScopeInstanceId scopeInstanceId,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstance> instances,
        IReadOnlyDictionary<EditorContributionId, EditorPanelDescriptor> panels,
        IReadOnlyDictionary<EditorCapabilityId, EditorModuleInstanceId> capabilityProviders)
    {
        ScopeInstanceId = scopeInstanceId;
        Instances = instances;
        Panels = panels;
        CapabilityProviders = capabilityProviders;
    }

    public ScopeInstanceId ScopeInstanceId { get; }

    public IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstance> Instances { get; }

    public IReadOnlyDictionary<EditorContributionId, EditorPanelDescriptor> Panels { get; }

    public IReadOnlyDictionary<EditorCapabilityId, EditorModuleInstanceId> CapabilityProviders { get; }
}

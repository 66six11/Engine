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
        IReadOnlyList<EditorModuleInstance> registrationOrder,
        IReadOnlyDictionary<EditorContributionId, EditorPanelDescriptor> panels,
        IReadOnlyDictionary<EditorCapabilityId, EditorModuleInstanceId> capabilityProviders,
        IReadOnlySet<EditorCapabilityId> hostCapabilities)
    {
        ScopeInstanceId = scopeInstanceId;
        Instances = instances;
        RegistrationOrder = registrationOrder;
        Panels = panels;
        CapabilityProviders = capabilityProviders;
        HostCapabilities = hostCapabilities;
    }

    public ScopeInstanceId ScopeInstanceId { get; }

    public IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstance> Instances { get; }

    public IReadOnlyList<EditorModuleInstance> RegistrationOrder { get; }

    public IReadOnlyDictionary<EditorContributionId, EditorPanelDescriptor> Panels { get; }

    public IReadOnlyDictionary<EditorCapabilityId, EditorModuleInstanceId> CapabilityProviders { get; }

    public IReadOnlySet<EditorCapabilityId> HostCapabilities { get; }
}

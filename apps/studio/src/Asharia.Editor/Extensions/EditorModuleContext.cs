using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Editor.Extensions;

public sealed class EditorModuleContext
{
    public EditorModuleContext(
        EditorModuleInstanceId instanceId,
        IEnumerable<EditorCapabilitySnapshot> capabilities)
    {
        if (!instanceId.IsValid)
        {
            throw new ArgumentException(
                "Module instance identity is invalid.",
                nameof(instanceId));
        }

        InstanceId = instanceId;
        Capabilities = CopyCapabilities(capabilities);
    }

    public EditorModuleInstanceId InstanceId { get; }

    public IReadOnlyList<EditorCapabilitySnapshot> Capabilities { get; }

    internal static IReadOnlyList<EditorCapabilitySnapshot> CopyCapabilities(
        IEnumerable<EditorCapabilitySnapshot> capabilities)
    {
        ArgumentNullException.ThrowIfNull(capabilities);

        var copy = capabilities.ToArray();
        for (var index = 0; index < copy.Length; index++)
        {
            if (!copy[index].IsValid)
            {
                throw new ArgumentException(
                    $"Capability snapshot at index {index} is invalid.",
                    nameof(capabilities));
            }
        }

        return Array.AsReadOnly(copy);
    }
}

public enum EditorModuleResumeReason
{
    ReloadRollback,
    CapabilityRecovered,
}

public sealed class EditorModuleResumeContext
{
    public EditorModuleResumeContext(
        EditorModuleResumeReason reason,
        ScopeInstanceId scopeInstanceId,
        IEnumerable<EditorCapabilitySnapshot> capabilities)
    {
        if (!Enum.IsDefined(reason))
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Module resume reason is invalid.");
        }

        if (!scopeInstanceId.IsValid)
        {
            throw new ArgumentException(
                "Scope instance identity is invalid.",
                nameof(scopeInstanceId));
        }

        Reason = reason;
        ScopeInstanceId = scopeInstanceId;
        Capabilities = EditorModuleContext.CopyCapabilities(capabilities);
    }

    public EditorModuleResumeReason Reason { get; }

    public ScopeInstanceId ScopeInstanceId { get; }

    public IReadOnlyList<EditorCapabilitySnapshot> Capabilities { get; }
}

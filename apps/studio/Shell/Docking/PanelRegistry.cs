using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public sealed class PanelRegistry : IPanelRegistry
{
    private readonly Dictionary<string, PanelDescriptor> descriptors_ = new(StringComparer.Ordinal);
    private readonly List<PanelDescriptor> descriptorsInRegistrationOrder_ = [];

    public void Register(PanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        if (string.IsNullOrWhiteSpace(descriptor.Id))
        {
            throw new ArgumentException("Panel id must not be empty.", nameof(descriptor));
        }

        if (!descriptors_.TryAdd(descriptor.Id, descriptor))
        {
            throw new InvalidOperationException($"Panel id '{descriptor.Id}' is already registered.");
        }

        descriptorsInRegistrationOrder_.Add(descriptor);
    }

    public IReadOnlyList<PanelDescriptor> GetAll()
    {
        return descriptorsInRegistrationOrder_.ToArray();
    }

    public PanelDescriptor GetRequired(string id)
    {
        if (descriptors_.TryGetValue(id, out var descriptor))
        {
            return descriptor;
        }

        throw new KeyNotFoundException($"Panel id '{id}' is not registered.");
    }
}

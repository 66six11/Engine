using System;
using System.Collections.Generic;

namespace Editor.Shell.Docking.Panels;

public sealed class PanelRegistry : IPanelRegistry
{
    private readonly Dictionary<string, PanelDescriptor> descriptors_ =
        new(StringComparer.Ordinal);
    private readonly List<PanelDescriptor> descriptorsInRegistrationOrder_ = [];

    public void Register(PanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.Id);
        if (!descriptors_.TryAdd(descriptor.Id, descriptor))
        {
            throw new InvalidOperationException(
                $"Dock panel id '{descriptor.Id}' is already registered.");
        }
        descriptorsInRegistrationOrder_.Add(descriptor);
    }

    public IReadOnlyList<PanelDescriptor> GetAll() =>
        descriptorsInRegistrationOrder_.ToArray();

    public PanelDescriptor GetRequired(string id) =>
        descriptors_.TryGetValue(id, out var descriptor)
            ? descriptor
            : throw new KeyNotFoundException(
                $"Dock panel id '{id}' is not registered.");
}

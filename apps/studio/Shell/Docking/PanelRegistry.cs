using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public sealed class PanelRegistry : IPanelRegistry
{
    private static readonly EditorExtensionId DirectRegistrationOwnerId = new("studio.panel-registry.direct");

    private readonly Dictionary<string, PanelRegistryEntry> descriptors_ = new(StringComparer.Ordinal);
    private readonly List<PanelRegistryEntry> descriptorsInRegistrationOrder_ = [];
    private long nextRegistrationId_;

    public void Register(PanelDescriptor descriptor)
    {
        _ = RegisterOwned(descriptor, DirectRegistrationOwnerId);
    }

    internal IDisposable RegisterOwned(PanelDescriptor descriptor, EditorExtensionId ownerId)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentNullException.ThrowIfNull(ownerId);

        if (string.IsNullOrWhiteSpace(descriptor.Id))
        {
            throw new ArgumentException("Panel id must not be empty.", nameof(descriptor));
        }

        var entry = new PanelRegistryEntry(
            descriptor,
            ownerId,
            ++nextRegistrationId_);

        if (!descriptors_.TryAdd(descriptor.Id, entry))
        {
            throw new InvalidOperationException(
                $"Panel id '{descriptor.Id}' is already registered by "
                + $"'{descriptors_[descriptor.Id].OwnerId}'; new owner '{ownerId}' cannot register it.");
        }

        descriptorsInRegistrationOrder_.Add(entry);
        return new PanelRegistrationLease(this, descriptor.Id, entry.RegistrationId);
    }

    public IReadOnlyList<PanelDescriptor> GetAll()
    {
        var descriptors = new PanelDescriptor[descriptorsInRegistrationOrder_.Count];
        for (var index = 0; index < descriptors.Length; index++)
        {
            descriptors[index] = descriptorsInRegistrationOrder_[index].Descriptor;
        }

        return descriptors;
    }

    public PanelDescriptor GetRequired(string id)
    {
        if (descriptors_.TryGetValue(id, out var entry))
        {
            return entry.Descriptor;
        }

        throw new KeyNotFoundException($"Panel id '{id}' is not registered.");
    }

    internal EditorExtensionId GetOwnerId(string id)
    {
        if (descriptors_.TryGetValue(id, out var entry))
        {
            return entry.OwnerId;
        }

        throw new KeyNotFoundException($"Panel id '{id}' is not registered.");
    }

    private void RemoveRegistration(string id, long registrationId)
    {
        if (!descriptors_.TryGetValue(id, out var entry)
            || entry.RegistrationId != registrationId)
        {
            return;
        }

        descriptors_.Remove(id);
        var index = descriptorsInRegistrationOrder_.FindIndex(
            item => item.RegistrationId == registrationId);
        if (index >= 0)
        {
            descriptorsInRegistrationOrder_.RemoveAt(index);
        }
    }

    private sealed record PanelRegistryEntry(
        PanelDescriptor Descriptor,
        EditorExtensionId OwnerId,
        long RegistrationId);

    private sealed class PanelRegistrationLease(
        PanelRegistry registry,
        string id,
        long registrationId) : IDisposable
    {
        private PanelRegistry? registry_ = registry;

        public void Dispose()
        {
            registry_?.RemoveRegistration(id, registrationId);
            registry_ = null;
        }
    }
}

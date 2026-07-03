using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

public sealed class WorkbenchActionRegistry : IWorkbenchActionRegistry
{
    private static readonly EditorExtensionId DirectRegistrationOwnerId =
        new("studio.workbench-action-registry.direct");

    private readonly Dictionary<string, WorkbenchActionRegistryEntry> descriptors_ =
        new(StringComparer.Ordinal);
    private readonly List<WorkbenchActionRegistryEntry> descriptorsInRegistrationOrder_ = [];
    private long nextRegistrationId_;

    public void Register(WorkbenchActionDescriptor descriptor)
    {
        _ = RegisterOwned(descriptor, DirectRegistrationOwnerId);
    }

    internal IDisposable RegisterOwned(
        WorkbenchActionDescriptor descriptor,
        EditorExtensionId ownerId)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentNullException.ThrowIfNull(ownerId);

        if (string.IsNullOrWhiteSpace(descriptor.Id))
        {
            throw new ArgumentException("Workbench action id must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Title))
        {
            throw new ArgumentException("Workbench action title must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.MenuPath))
        {
            throw new ArgumentException("Workbench action menu path must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Category))
        {
            throw new ArgumentException("Workbench action category must not be empty.", nameof(descriptor));
        }

        if (!descriptor.IsEnabled && string.IsNullOrWhiteSpace(descriptor.DisabledReason))
        {
            throw new ArgumentException("Disabled workbench actions must specify a disabled reason.", nameof(descriptor));
        }

        if (descriptor.Kind == WorkbenchActionKind.OpenPanel
            && string.IsNullOrWhiteSpace(descriptor.TargetId))
        {
            throw new ArgumentException("OpenPanel workbench actions must specify a target panel id.", nameof(descriptor));
        }

        var entry = new WorkbenchActionRegistryEntry(
            descriptor,
            ownerId,
            ++nextRegistrationId_);

        if (!descriptors_.TryAdd(descriptor.Id, entry))
        {
            throw new InvalidOperationException(
                $"Workbench action id '{descriptor.Id}' is already registered by "
                + $"'{descriptors_[descriptor.Id].OwnerId}'; new owner '{ownerId}' cannot register it.");
        }

        descriptorsInRegistrationOrder_.Add(entry);
        return new WorkbenchActionRegistrationLease(
            this,
            descriptor.Id,
            entry.RegistrationId);
    }

    public IReadOnlyList<WorkbenchActionDescriptor> GetAll()
    {
        var descriptors = new WorkbenchActionDescriptor[descriptorsInRegistrationOrder_.Count];
        for (var index = 0; index < descriptors.Length; index++)
        {
            descriptors[index] = descriptorsInRegistrationOrder_[index].Descriptor;
        }

        return descriptors;
    }

    public WorkbenchActionDescriptor? FindById(string id)
    {
        ArgumentNullException.ThrowIfNull(id);

        return descriptors_.GetValueOrDefault(id)?.Descriptor;
    }

    internal EditorExtensionId GetOwnerId(string id)
    {
        if (descriptors_.TryGetValue(id, out var entry))
        {
            return entry.OwnerId;
        }

        throw new KeyNotFoundException($"Workbench action id '{id}' is not registered.");
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

    private sealed record WorkbenchActionRegistryEntry(
        WorkbenchActionDescriptor Descriptor,
        EditorExtensionId OwnerId,
        long RegistrationId);

    private sealed class WorkbenchActionRegistrationLease(
        WorkbenchActionRegistry registry,
        string id,
        long registrationId) : IDisposable
    {
        private WorkbenchActionRegistry? registry_ = registry;

        public void Dispose()
        {
            registry_?.RemoveRegistration(id, registrationId);
            registry_ = null;
        }
    }
}

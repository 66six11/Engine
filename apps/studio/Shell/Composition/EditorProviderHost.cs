using System;
using System.Collections.Generic;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Abstractions;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Scene;

namespace Editor.Shell.Composition;

internal sealed class EditorProviderHost : IDisposable
{
    private readonly Dictionary<string, SceneProviderEntry> providersById_ =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, SceneProviderEntry> providersByRole_ =
        new(StringComparer.Ordinal);
    private readonly List<SceneProviderEntry> providersInRegistrationOrder_ = [];
    private long nextRegistrationId_;

    public IDisposable RegisterOwned(
        SceneProviderDescriptor descriptor,
        EditorExtensionId ownerId)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentNullException.ThrowIfNull(ownerId);

        if (providersById_.TryGetValue(descriptor.Id, out var existingById))
        {
            throw new InvalidOperationException(
                $"Scene provider id '{descriptor.Id}' is already registered by "
                + $"'{existingById.OwnerId}'; new owner '{ownerId}' cannot register it.");
        }

        if (providersByRole_.TryGetValue(descriptor.Role, out var existingByRole))
        {
            throw new InvalidOperationException(
                $"Scene provider role '{descriptor.Role}' is already registered by "
                + $"'{existingByRole.OwnerId}'; new owner '{ownerId}' cannot register it.");
        }

        var entry = new SceneProviderEntry(
            descriptor,
            ownerId,
            ++nextRegistrationId_);
        providersById_.Add(descriptor.Id, entry);
        providersByRole_.Add(descriptor.Role, entry);
        providersInRegistrationOrder_.Add(entry);
        return new SceneProviderRegistrationLease(this, descriptor.Id, entry.RegistrationId);
    }

    public IReadOnlyList<SceneProviderDescriptor> GetSceneProviders()
    {
        var descriptors = new SceneProviderDescriptor[providersInRegistrationOrder_.Count];
        for (var index = 0; index < descriptors.Length; index++)
        {
            descriptors[index] = providersInRegistrationOrder_[index].Descriptor;
        }

        return descriptors;
    }

    public ISceneSnapshotProvider GetRequiredSceneSnapshotProvider(string role)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(role);

        if (!providersByRole_.TryGetValue(role, out var entry))
        {
            throw new KeyNotFoundException($"Scene provider role '{role}' is not registered.");
        }

        return entry.GetOrCreateProvider();
    }

    public EditorProviderStatusSnapshot GetStatus(string id)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);

        if (providersById_.TryGetValue(id, out var entry))
        {
            return entry.GetStatus();
        }

        throw new KeyNotFoundException($"Scene provider id '{id}' is not registered.");
    }

    public EditorExtensionId GetOwnerId(string id)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);

        if (providersById_.TryGetValue(id, out var entry))
        {
            return entry.OwnerId;
        }

        throw new KeyNotFoundException($"Scene provider id '{id}' is not registered.");
    }

    public void Dispose()
    {
        for (var index = providersInRegistrationOrder_.Count - 1; index >= 0; index--)
        {
            providersInRegistrationOrder_[index].DisposeProvider();
        }

        providersById_.Clear();
        providersByRole_.Clear();
        providersInRegistrationOrder_.Clear();
    }

    private void RemoveRegistration(string id, long registrationId)
    {
        if (!providersById_.TryGetValue(id, out var entry)
            || entry.RegistrationId != registrationId)
        {
            return;
        }

        try
        {
            entry.DisposeProvider();
        }
        finally
        {
            providersById_.Remove(id);
            providersByRole_.Remove(entry.Descriptor.Role);
            var index = providersInRegistrationOrder_.FindIndex(
                item => item.RegistrationId == registrationId);
            if (index >= 0)
            {
                providersInRegistrationOrder_.RemoveAt(index);
            }
        }
    }

    private sealed class SceneProviderEntry(
        SceneProviderDescriptor descriptor,
        EditorExtensionId ownerId,
        long registrationId)
    {
        private ISceneSnapshotProvider? provider_;
        private EditorProviderState state_ = EditorProviderState.Created;
        private string? message_;

        public SceneProviderDescriptor Descriptor { get; } = descriptor;

        public EditorExtensionId OwnerId { get; } = ownerId;

        public long RegistrationId { get; } = registrationId;

        public ISceneSnapshotProvider GetOrCreateProvider()
        {
            if (provider_ is not null)
            {
                return provider_;
            }

            try
            {
                var provider = Descriptor.CreateProvider();
                if (provider is null)
                {
                    throw new InvalidOperationException("Scene provider factory returned null.");
                }

                provider_ = provider;
                state_ = EditorProviderState.Ready;
                message_ = null;
                return provider_;
            }
            catch (Exception exception)
            {
                state_ = EditorProviderState.Faulted;
                message_ = exception.Message;
                throw new InvalidOperationException(
                    $"Scene provider '{Descriptor.Id}' failed to create.",
                    exception);
            }
        }

        public EditorProviderStatusSnapshot GetStatus()
        {
            return new EditorProviderStatusSnapshot(
                Descriptor.Id,
                Descriptor.Role,
                OwnerId,
                state_,
                message_);
        }

        public void DisposeProvider()
        {
            try
            {
                if (provider_ is IDisposable disposable)
                {
                    disposable.Dispose();
                }
            }
            finally
            {
                provider_ = null;
            }
        }
    }

    private sealed class SceneProviderRegistrationLease(
        EditorProviderHost host,
        string id,
        long registrationId) : IDisposable
    {
        private EditorProviderHost? host_ = host;

        public void Dispose()
        {
            host_?.RemoveRegistration(id, registrationId);
            host_ = null;
        }
    }
}

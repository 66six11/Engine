using System;
using System.Collections.Generic;
using Asharia.Editor.Extensions;
using Asharia.Editor.Worlds.Snapshots;

namespace Asharia.Studio.Application.Providers;

public sealed class EditorProviderHost : IDisposable
{
    private readonly Dictionary<string, SceneProviderEntry> providersById_ =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, SceneProviderEntry> providersByRole_ =
        new(StringComparer.Ordinal);
    private readonly List<SceneProviderEntry> providersInRegistrationOrder_ = [];
    private long nextRegistrationId_;

    public IDisposable RegisterOwned(
        EditorSceneProviderRegistration registration,
        EditorModuleDefinitionId ownerId)
    {
        ArgumentNullException.ThrowIfNull(registration);
        if (!ownerId.IsValid)
        {
            throw new ArgumentException("Module definition identity is invalid.", nameof(ownerId));
        }

        if (providersById_.TryGetValue(registration.Id, out var existingById))
        {
            throw new InvalidOperationException(
                $"Scene provider id '{registration.Id}' is already registered by "
                + $"'{ownerName(existingById.OwnerId)}'; new owner "
                + $"'{ownerName(ownerId)}' cannot register it.");
        }

        if (providersByRole_.TryGetValue(registration.Role, out var existingByRole))
        {
            throw new InvalidOperationException(
                $"Scene provider role '{registration.Role}' is already registered by "
                + $"'{ownerName(existingByRole.OwnerId)}'; new owner "
                + $"'{ownerName(ownerId)}' cannot register it.");
        }

        var entry = new SceneProviderEntry(
            registration,
            ownerId,
            ++nextRegistrationId_);
        providersById_.Add(registration.Id, entry);
        providersByRole_.Add(registration.Role, entry);
        providersInRegistrationOrder_.Add(entry);
        return new SceneProviderRegistrationLease(this, registration.Id, entry.RegistrationId);
    }

    public IReadOnlyList<EditorSceneProviderRegistration> GetSceneProviders()
    {
        var registrations =
            new EditorSceneProviderRegistration[providersInRegistrationOrder_.Count];
        for (var index = 0; index < registrations.Length; index++)
        {
            registrations[index] = providersInRegistrationOrder_[index].Registration;
        }

        return registrations;
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

    public EditorModuleDefinitionId GetOwnerId(string id)
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
        List<Exception>? failures = null;
        try
        {
            for (var index = providersInRegistrationOrder_.Count - 1; index >= 0; index--)
            {
                try
                {
                    providersInRegistrationOrder_[index].DisposeProvider();
                }
                catch (Exception exception)
                {
                    failures ??= [];
                    failures.Add(exception);
                }
            }
        }
        finally
        {
            providersById_.Clear();
            providersByRole_.Clear();
            providersInRegistrationOrder_.Clear();
        }

        if (failures?.Count == 1)
        {
            throw failures[0];
        }

        if (failures is not null)
        {
            throw new AggregateException(failures);
        }
    }

    private static string ownerName(EditorModuleDefinitionId ownerId)
    {
        return ownerId.Module.Value;
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
            providersByRole_.Remove(entry.Registration.Role);
            var index = providersInRegistrationOrder_.FindIndex(
                item => item.RegistrationId == registrationId);
            if (index >= 0)
            {
                providersInRegistrationOrder_.RemoveAt(index);
            }
        }
    }

    private sealed class SceneProviderEntry(
        EditorSceneProviderRegistration registration,
        EditorModuleDefinitionId ownerId,
        long registrationId)
    {
        private ISceneSnapshotProvider? provider_;
        private EditorProviderState state_ = EditorProviderState.Created;
        private string? message_;

        public EditorSceneProviderRegistration Registration { get; } = registration;

        public EditorModuleDefinitionId OwnerId { get; } = ownerId;

        public long RegistrationId { get; } = registrationId;

        public ISceneSnapshotProvider GetOrCreateProvider()
        {
            if (provider_ is not null)
            {
                return provider_;
            }

            try
            {
                var provider = Registration.CreateProvider();
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
                    $"Scene provider '{Registration.Id}' failed to create.",
                    exception);
            }
        }

        public EditorProviderStatusSnapshot GetStatus()
        {
            return new EditorProviderStatusSnapshot(
                Registration.Id,
                Registration.Role,
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
            var host = host_;
            host_ = null;
            host?.RemoveRegistration(id, registrationId);
        }
    }
}

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorModuleRegistry
{
    private readonly object gate_ = new();
    private EditorModuleRegistrySnapshot snapshot_ = EditorModuleRegistrySnapshot.Empty;

    public bool TryGetPartition(
        ScopeInstanceId scopeInstanceId,
        out EditorScopePartition? partition)
    {
        if (!scopeInstanceId.IsValid)
        {
            partition = null;
            return false;
        }

        lock (gate_)
        {
            return snapshot_.Partitions.TryGetValue(scopeInstanceId, out partition);
        }
    }

    public EditorScopePartition GetRequiredPartition(ScopeInstanceId scopeInstanceId)
    {
        if (!TryGetPartition(scopeInstanceId, out var partition))
        {
            throw new KeyNotFoundException(
                $"Editor scope partition '{scopeInstanceId}' is not committed.");
        }

        return partition!;
    }

    internal EditorModuleRegistrySnapshot CaptureSnapshot()
    {
        lock (gate_)
        {
            return snapshot_;
        }
    }

    internal void Commit(
        EditorModuleRegistrySnapshot expectedSnapshot,
        EditorScopePartition candidate)
    {
        ArgumentNullException.ThrowIfNull(expectedSnapshot);
        ArgumentNullException.ThrowIfNull(candidate);

        lock (gate_)
        {
            if (!ReferenceEquals(snapshot_, expectedSnapshot))
            {
                throw new InvalidOperationException(
                    "The editor module registry changed after this scope transaction was prepared.");
            }

            var partitions = new Dictionary<ScopeInstanceId, EditorScopePartition>(
                snapshot_.Partitions)
            {
                [candidate.ScopeInstanceId] = candidate,
            };
            snapshot_ = new EditorModuleRegistrySnapshot(
                new ReadOnlyDictionary<ScopeInstanceId, EditorScopePartition>(partitions));
        }
    }
}

internal sealed class EditorModuleRegistrySnapshot
{
    public static EditorModuleRegistrySnapshot Empty { get; } = new(
        new ReadOnlyDictionary<ScopeInstanceId, EditorScopePartition>(
            new Dictionary<ScopeInstanceId, EditorScopePartition>()));

    public EditorModuleRegistrySnapshot(
        IReadOnlyDictionary<ScopeInstanceId, EditorScopePartition> partitions)
    {
        Partitions = partitions;
    }

    public IReadOnlyDictionary<ScopeInstanceId, EditorScopePartition> Partitions { get; }
}

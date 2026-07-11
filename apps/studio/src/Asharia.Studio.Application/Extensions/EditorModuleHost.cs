using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public enum EditorModuleInstanceState
{
    Dormant,
    WaitingForCapability,
    Active,
    Faulted,
    Blocked,
    Disposed,
}

public sealed class EditorModuleInstanceStatus
{
    internal EditorModuleInstanceStatus(EditorModuleInstanceId id)
    {
        Id = id;
    }

    public EditorModuleInstanceId Id { get; }

    public EditorModuleInstanceState State { get; internal set; }

    public Exception? Failure { get; internal set; }
}

public sealed class EditorModuleHost : IAsyncDisposable
{
    private readonly object gate_ = new();
    private readonly Dictionary<ScopeInstanceId, Task<EditorScopeActivation>> scopeTasks_ = [];
    private bool isDisposed_;

    public ValueTask<EditorScopeActivation> ActivateScopeAsync(
        EditorScopePartition partition,
        IEnumerable<EditorCapabilitySnapshot> capabilities,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(partition);
        var capabilityArray = CopyCapabilities(capabilities);
        TaskCompletionSource<EditorScopeActivation>? completion = null;
        Task<EditorScopeActivation> task;

        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            if (scopeTasks_.TryGetValue(partition.ScopeInstanceId, out task!))
            {
                return new ValueTask<EditorScopeActivation>(task);
            }

            completion = new TaskCompletionSource<EditorScopeActivation>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            task = completion.Task;
            scopeTasks_.Add(partition.ScopeInstanceId, task);
        }

        _ = CompleteActivationAsync(
            partition,
            capabilityArray,
            cancellationToken,
            completion);
        return new ValueTask<EditorScopeActivation>(task);
    }

    public async ValueTask DisposeAsync()
    {
        Task<EditorScopeActivation>[] tasks;
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            isDisposed_ = true;
            tasks = scopeTasks_.Values.Reverse().ToArray();
            scopeTasks_.Clear();
        }

        var failures = new List<Exception>();
        foreach (var task in tasks)
        {
            try
            {
                var scope = await task.ConfigureAwait(false);
                await scope.DisposeAsync().ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        if (failures.Count > 0)
        {
            throw new AggregateException("One or more editor scopes failed to dispose.", failures);
        }
    }

    private async Task CompleteActivationAsync(
        EditorScopePartition partition,
        IReadOnlyList<EditorCapabilitySnapshot> capabilities,
        CancellationToken cancellationToken,
        TaskCompletionSource<EditorScopeActivation> completion)
    {
        try
        {
            var scope = await EditorScopeActivation.CreateAsync(
                partition,
                capabilities,
                RemoveScope,
                cancellationToken).ConfigureAwait(false);
            completion.SetResult(scope);
        }
        catch (OperationCanceledException exception)
            when (exception.CancellationToken == cancellationToken)
        {
            RemoveScope(partition.ScopeInstanceId);
            completion.SetCanceled(cancellationToken);
        }
        catch (Exception exception)
        {
            RemoveScope(partition.ScopeInstanceId);
            completion.SetException(exception);
        }
    }

    private void RemoveScope(ScopeInstanceId scopeInstanceId)
    {
        lock (gate_)
        {
            scopeTasks_.Remove(scopeInstanceId);
        }
    }

    private static IReadOnlyList<EditorCapabilitySnapshot> CopyCapabilities(
        IEnumerable<EditorCapabilitySnapshot> capabilities)
    {
        ArgumentNullException.ThrowIfNull(capabilities);
        var copy = capabilities.ToArray();
        var ids = new HashSet<EditorCapabilityId>();
        for (var index = 0; index < copy.Length; index++)
        {
            if (!copy[index].IsValid)
            {
                throw new ArgumentException(
                    $"Capability snapshot at index {index} is invalid.",
                    nameof(capabilities));
            }

            if (!ids.Add(copy[index].Id))
            {
                throw new ArgumentException(
                    $"Capability snapshot '{copy[index].Id}' is duplicated.",
                    nameof(capabilities));
            }
        }

        return Array.AsReadOnly(copy);
    }
}

public sealed class EditorScopeActivation : IAsyncDisposable
{
    private readonly object gate_ = new();
    private readonly IReadOnlyList<ActivatedLease> activatedLeases_;
    private readonly Action<ScopeInstanceId> releaseScope_;
    private Task? disposalTask_;

    private EditorScopeActivation(
        ScopeInstanceId scopeInstanceId,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus> instances,
        IReadOnlyList<ActivatedLease> activatedLeases,
        Action<ScopeInstanceId> releaseScope)
    {
        ScopeInstanceId = scopeInstanceId;
        Instances = instances;
        activatedLeases_ = activatedLeases;
        releaseScope_ = releaseScope;
    }

    public ScopeInstanceId ScopeInstanceId { get; }

    public IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus> Instances
    {
        get;
    }

    public ValueTask DisposeAsync()
    {
        lock (gate_)
        {
            disposalTask_ ??= DisposeCoreAsync();
            return new ValueTask(disposalTask_);
        }
    }

    internal static async Task<EditorScopeActivation> CreateAsync(
        EditorScopePartition partition,
        IReadOnlyList<EditorCapabilitySnapshot> capabilities,
        Action<ScopeInstanceId> releaseScope,
        CancellationToken cancellationToken)
    {
        var statuses = partition.RegistrationOrder.ToDictionary(
            instance => instance.Definition.Id,
            instance => new EditorModuleInstanceStatus(instance.Id));
        var capabilityMap = capabilities.ToDictionary(snapshot => snapshot.Id);
        var activatedLeases = new List<ActivatedLease>();
        var targets = BuildActivationTargets(partition);
        var activationOrder = BuildActivationOrder(partition, targets);

        try
        {
            foreach (var instance in activationOrder)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var status = statuses[instance.Definition.Id];
                if (HasBlockedDependency(partition, instance, statuses))
                {
                    status.State = EditorModuleInstanceState.Blocked;
                    continue;
                }

                if (!RequiredCapabilitiesAreReady(
                        partition,
                        instance,
                        statuses,
                        capabilityMap,
                        out var blocked))
                {
                    status.State = blocked
                        ? EditorModuleInstanceState.Blocked
                        : EditorModuleInstanceState.WaitingForCapability;
                    continue;
                }

                try
                {
                    var context = new EditorModuleContext(
                        instance.Id,
                        BuildContextCapabilities(partition, statuses, capabilityMap));
                    var lease = await instance.Definition.Module.ActivateAsync(
                        context,
                        cancellationToken).ConfigureAwait(false);
                    if (lease is null)
                    {
                        throw new InvalidOperationException(
                            $"Module '{instance.Definition.Id}' returned a null activation lease.");
                    }

                    status.State = EditorModuleInstanceState.Active;
                    activatedLeases.Add(new ActivatedLease(instance.Definition.Id, lease));
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception exception)
                {
                    status.State = EditorModuleInstanceState.Faulted;
                    status.Failure = exception;
                }
            }
        }
        catch
        {
            await DisposeLeasesAsync(
                BuildDisposalOrder(partition, activatedLeases),
                statuses.Values).ConfigureAwait(false);
            throw;
        }

        return new EditorScopeActivation(
            partition.ScopeInstanceId,
            new ReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus>(statuses),
            BuildDisposalOrder(partition, activatedLeases),
            releaseScope);
    }

    private async Task DisposeCoreAsync()
    {
        try
        {
            await DisposeLeasesAsync(activatedLeases_, Instances.Values).ConfigureAwait(false);
        }
        finally
        {
            releaseScope_(ScopeInstanceId);
        }
    }

    private static HashSet<EditorModuleDefinitionId> BuildActivationTargets(
        EditorScopePartition partition)
    {
        var targets = new HashSet<EditorModuleDefinitionId>();

        void Include(EditorModuleDefinitionId definitionId)
        {
            if (!targets.Add(definitionId)
                || !partition.Instances.TryGetValue(definitionId, out var instance))
            {
                return;
            }

            foreach (var dependency in instance.Definition.Declaration.RequiredModules)
            {
                Include(dependency);
            }

            foreach (var capability in instance.Definition.Declaration.RequiredCapabilities)
            {
                if (partition.CapabilityProviders.TryGetValue(capability, out var provider))
                {
                    Include(provider.Definition);
                }
            }
        }

        foreach (var instance in partition.RegistrationOrder)
        {
            if (instance.Definition.Metadata.Activation == EditorModuleActivationPolicy.OnScopeReady)
            {
                Include(instance.Definition.Id);
            }
        }

        return targets;
    }

    private static IReadOnlyList<EditorModuleInstance> BuildActivationOrder(
        EditorScopePartition partition,
        IReadOnlySet<EditorModuleDefinitionId> targets)
    {
        var order = new List<EditorModuleInstance>();
        var remainingDependencies = targets.ToDictionary(definitionId => definitionId, _ => 0);
        var dependents = targets.ToDictionary(
            definitionId => definitionId,
            _ => new List<EditorModuleDefinitionId>());
        foreach (var definitionId in targets)
        {
            foreach (var dependency in GetLocalDependencies(
                         partition,
                         partition.Instances[definitionId]))
            {
                if (!targets.Contains(dependency))
                {
                    continue;
                }

                remainingDependencies[definitionId]++;
                dependents[dependency].Add(definitionId);
            }
        }

        var visited = new HashSet<EditorModuleDefinitionId>();
        while (visited.Count < targets.Count)
        {
            EditorModuleInstance? next = null;
            foreach (var instance in partition.RegistrationOrder)
            {
                if (targets.Contains(instance.Definition.Id)
                    && !visited.Contains(instance.Definition.Id)
                    && remainingDependencies[instance.Definition.Id] == 0)
                {
                    next = instance;
                    break;
                }
            }

            if (next is null)
            {
                throw new InvalidOperationException("The activation dependency graph contains a cycle.");
            }

            var nextId = next.Definition.Id;
            visited.Add(nextId);
            order.Add(next);
            foreach (var dependent in dependents[nextId])
            {
                remainingDependencies[dependent]--;
            }
        }

        return order;
    }

    private static IReadOnlyList<ActivatedLease> BuildDisposalOrder(
        EditorScopePartition partition,
        IReadOnlyList<ActivatedLease> activatedLeases)
    {
        var leases = activatedLeases.ToDictionary(lease => lease.DefinitionId);
        var remainingDependents = leases.Keys.ToDictionary(definitionId => definitionId, _ => 0);
        var dependencies = leases.Keys.ToDictionary(
            definitionId => definitionId,
            definitionId => GetLocalDependencies(partition, partition.Instances[definitionId])
                .Where(leases.ContainsKey)
                .ToArray());
        foreach (var instanceDependencies in dependencies.Values)
        {
            foreach (var dependency in instanceDependencies)
            {
                remainingDependents[dependency]++;
            }
        }

        var disposalOrder = new List<ActivatedLease>();
        var visited = new HashSet<EditorModuleDefinitionId>();
        while (visited.Count < leases.Count)
        {
            EditorModuleDefinitionId? next = null;
            for (var index = partition.RegistrationOrder.Count - 1; index >= 0; index--)
            {
                var definitionId = partition.RegistrationOrder[index].Definition.Id;
                if (leases.ContainsKey(definitionId)
                    && !visited.Contains(definitionId)
                    && remainingDependents[definitionId] == 0)
                {
                    next = definitionId;
                    break;
                }
            }

            if (next is null)
            {
                throw new InvalidOperationException("The activation dependency graph contains a cycle.");
            }

            visited.Add(next.Value);
            disposalOrder.Add(leases[next.Value]);
            foreach (var dependency in dependencies[next.Value])
            {
                remainingDependents[dependency]--;
            }
        }

        return disposalOrder.AsReadOnly();
    }

    private static IReadOnlyList<EditorModuleDefinitionId> GetLocalDependencies(
        EditorScopePartition partition,
        EditorModuleInstance instance)
    {
        var dependencies = new HashSet<EditorModuleDefinitionId>();
        foreach (var dependency in instance.Definition.Declaration.RequiredModules)
        {
            if (partition.Instances.ContainsKey(dependency))
            {
                dependencies.Add(dependency);
            }
        }

        foreach (var capability in instance.Definition.Declaration.RequiredCapabilities)
        {
            if (partition.CapabilityProviders.TryGetValue(capability, out var provider))
            {
                dependencies.Add(provider.Definition);
            }
        }

        return dependencies.ToArray();
    }

    private static bool HasBlockedDependency(
        EditorScopePartition partition,
        EditorModuleInstance instance,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus> statuses)
    {
        foreach (var dependency in instance.Definition.Declaration.RequiredModules)
        {
            if (partition.Instances.ContainsKey(dependency)
                && statuses[dependency].State != EditorModuleInstanceState.Active)
            {
                return true;
            }
        }

        return false;
    }

    private static bool RequiredCapabilitiesAreReady(
        EditorScopePartition partition,
        EditorModuleInstance instance,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus> statuses,
        IReadOnlyDictionary<EditorCapabilityId, EditorCapabilitySnapshot> capabilityMap,
        out bool blocked)
    {
        blocked = false;
        foreach (var capability in instance.Definition.Declaration.RequiredCapabilities)
        {
            if (partition.CapabilityProviders.TryGetValue(capability, out var provider))
            {
                if (statuses[provider.Definition].State != EditorModuleInstanceState.Active)
                {
                    blocked = true;
                    return false;
                }

                continue;
            }

            if (!capabilityMap.TryGetValue(capability, out var snapshot)
                || snapshot.State != EditorCapabilityState.Ready)
            {
                return false;
            }
        }

        return true;
    }

    private static IReadOnlyList<EditorCapabilitySnapshot> BuildContextCapabilities(
        EditorScopePartition partition,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstanceStatus> statuses,
        IReadOnlyDictionary<EditorCapabilityId, EditorCapabilitySnapshot> capabilityMap)
    {
        var snapshots = new Dictionary<EditorCapabilityId, EditorCapabilitySnapshot>(capabilityMap);
        foreach (var pair in partition.CapabilityProviders)
        {
            if (statuses[pair.Value.Definition].State == EditorModuleInstanceState.Active)
            {
                var epoch = snapshots.TryGetValue(pair.Key, out var existing)
                    ? existing.Epoch
                    : 0;
                snapshots[pair.Key] = EditorCapabilitySnapshot.Create(
                    pair.Key,
                    epoch,
                    EditorCapabilityState.Ready);
            }
        }

        return Array.AsReadOnly(snapshots.Values.ToArray());
    }

    private static async Task DisposeLeasesAsync(
        IReadOnlyList<ActivatedLease> activatedLeases,
        IEnumerable<EditorModuleInstanceStatus> statuses)
    {
        var failures = new List<Exception>();
        for (var index = 0; index < activatedLeases.Count; index++)
        {
            try
            {
                await activatedLeases[index].Lease.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        foreach (var status in statuses)
        {
            status.State = EditorModuleInstanceState.Disposed;
        }

        if (failures.Count > 0)
        {
            throw new AggregateException(
                "One or more editor module activation leases failed to dispose.",
                failures);
        }
    }

    private sealed record ActivatedLease(
        EditorModuleDefinitionId DefinitionId,
        IEditorModuleActivation Lease);
}

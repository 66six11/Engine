using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Editor.Contributions;
using Asharia.Editor.Extensions;
using Asharia.Editor.Panels;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorScopeTransaction
{
    private readonly EditorModuleRegistry registry_;
    private readonly EditorModuleRegistrySnapshot expectedSnapshot_;
    private bool isCommitted_;

    private EditorScopeTransaction(
        EditorModuleRegistry registry,
        EditorModuleRegistrySnapshot expectedSnapshot,
        EditorScopePartition candidate)
    {
        registry_ = registry;
        expectedSnapshot_ = expectedSnapshot;
        Candidate = candidate;
    }

    public EditorScopePartition Candidate { get; }

    public static EditorScopeTransaction Prepare(
        EditorModuleRegistry registry,
        ScopeInstanceId scopeInstanceId,
        IEnumerable<EditorModuleDefinition> definitions)
    {
        ArgumentNullException.ThrowIfNull(registry);
        if (!scopeInstanceId.IsValid)
        {
            throw new ArgumentException(
                "Scope instance identity is invalid.",
                nameof(scopeInstanceId));
        }

        ArgumentNullException.ThrowIfNull(definitions);
        var definitionArray = definitions.ToArray();
        var expectedSnapshot = registry.CaptureSnapshot();
        var candidate = BuildCandidate(expectedSnapshot, scopeInstanceId, definitionArray);
        return new EditorScopeTransaction(registry, expectedSnapshot, candidate);
    }

    public void Commit()
    {
        if (isCommitted_)
        {
            throw new InvalidOperationException("The editor scope transaction is already committed.");
        }

        registry_.Commit(expectedSnapshot_, Candidate);
        isCommitted_ = true;
    }

    private static EditorScopePartition BuildCandidate(
        EditorModuleRegistrySnapshot snapshot,
        ScopeInstanceId scopeInstanceId,
        IReadOnlyList<EditorModuleDefinition> definitions)
    {
        var diagnostics = new List<string>();
        var definitionMap = new Dictionary<EditorModuleDefinitionId, EditorModuleDefinition>();
        var expectedScope = scopeInstanceId == ScopeInstanceId.Application
            ? EditorModuleScopeKind.Application
            : EditorModuleScopeKind.Project;

        for (var index = 0; index < definitions.Count; index++)
        {
            var definition = definitions[index];
            if (definition is null)
            {
                diagnostics.Add($"Module definition at index {index} is null.");
                continue;
            }

            if (definition.Id.Scope != expectedScope)
            {
                diagnostics.Add(
                    $"Module definition '{definition.Id}' does not match scope '{scopeInstanceId}'.");
                continue;
            }

            if (!definitionMap.TryAdd(definition.Id, definition))
            {
                diagnostics.Add($"Module definition '{definition.Id}' is duplicated in the candidate.");
            }
        }

        var visibleApplication = GetVisibleApplicationPartition(snapshot, scopeInstanceId);
        ValidateRequiredModules(definitionMap, visibleApplication, diagnostics);
        ValidateRequiredCycles(definitionMap, diagnostics);

        var instances = definitionMap.ToDictionary(
            pair => pair.Key,
            pair => new EditorModuleInstance(pair.Value, scopeInstanceId));
        var panels = BuildPanelMap(instances, visibleApplication, diagnostics);
        var capabilityProviders = BuildCapabilityProviderMap(
            instances,
            visibleApplication,
            diagnostics);
        ValidateRequiredCapabilities(
            definitionMap,
            visibleApplication,
            capabilityProviders,
            diagnostics);

        if (diagnostics.Count > 0)
        {
            throw new EditorScopeValidationException(diagnostics);
        }

        return new EditorScopePartition(
            scopeInstanceId,
            ReadOnly(instances),
            ReadOnly(panels),
            ReadOnly(capabilityProviders));
    }

    private static EditorScopePartition? GetVisibleApplicationPartition(
        EditorModuleRegistrySnapshot snapshot,
        ScopeInstanceId scopeInstanceId)
    {
        if (scopeInstanceId == ScopeInstanceId.Application)
        {
            return null;
        }

        return snapshot.Partitions.TryGetValue(ScopeInstanceId.Application, out var application)
            ? application
            : null;
    }

    private static void ValidateRequiredModules(
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> definitions,
        EditorScopePartition? visibleApplication,
        ICollection<string> diagnostics)
    {
        foreach (var definition in definitions.Values)
        {
            foreach (var dependency in definition.Declaration.RequiredModules)
            {
                var available = definitions.ContainsKey(dependency)
                    || (visibleApplication?.Instances.ContainsKey(dependency) ?? false);
                if (!available)
                {
                    diagnostics.Add(
                        $"Required module '{dependency}' for '{definition.Id}' is not available.");
                }
            }
        }
    }

    private static void ValidateRequiredCycles(
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> definitions,
        ICollection<string> diagnostics)
    {
        var states = new Dictionary<EditorModuleDefinitionId, int>();
        foreach (var definitionId in definitions.Keys)
        {
            if (Visit(definitionId, definitions, states))
            {
                diagnostics.Add(
                    $"Required module dependency cycle includes '{definitionId}'.");
                return;
            }
        }
    }

    private static bool Visit(
        EditorModuleDefinitionId definitionId,
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> definitions,
        IDictionary<EditorModuleDefinitionId, int> states)
    {
        if (states.TryGetValue(definitionId, out var state))
        {
            return state == 1;
        }

        states[definitionId] = 1;
        foreach (var dependency in definitions[definitionId].Declaration.RequiredModules)
        {
            if (definitions.ContainsKey(dependency)
                && Visit(dependency, definitions, states))
            {
                return true;
            }
        }

        states[definitionId] = 2;
        return false;
    }

    private static Dictionary<EditorContributionId, EditorPanelDescriptor> BuildPanelMap(
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstance> instances,
        EditorScopePartition? visibleApplication,
        ICollection<string> diagnostics)
    {
        var occupied = new HashSet<EditorContributionId>(
            visibleApplication?.Panels.Keys ?? []);
        var panels = new Dictionary<EditorContributionId, EditorPanelDescriptor>();
        foreach (var instance in instances.Values)
        {
            foreach (var panel in instance.Definition.Declaration.Panels)
            {
                if (!occupied.Add(panel.Id))
                {
                    diagnostics.Add($"Panel contribution '{panel.Id}' is duplicated across the scope.");
                    continue;
                }

                panels.Add(panel.Id, panel);
            }
        }

        return panels;
    }

    private static Dictionary<EditorCapabilityId, EditorModuleInstanceId>
        BuildCapabilityProviderMap(
            IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleInstance> instances,
            EditorScopePartition? visibleApplication,
            ICollection<string> diagnostics)
    {
        var occupied = new Dictionary<EditorCapabilityId, EditorModuleInstanceId>();
        if (visibleApplication is not null)
        {
            foreach (var pair in visibleApplication.CapabilityProviders)
            {
                occupied.Add(pair.Key, pair.Value);
            }
        }

        var providers = new Dictionary<EditorCapabilityId, EditorModuleInstanceId>();
        foreach (var instance in instances.Values)
        {
            foreach (var capability in instance.Definition.Declaration.ProvidedCapabilities)
            {
                if (occupied.ContainsKey(capability))
                {
                    diagnostics.Add(
                        $"Capability provider for '{capability}' is ambiguous across the scope.");
                    continue;
                }

                occupied.Add(capability, instance.Id);
                providers.Add(capability, instance.Id);
            }
        }

        return providers;
    }

    private static void ValidateRequiredCapabilities(
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> definitions,
        EditorScopePartition? visibleApplication,
        IReadOnlyDictionary<EditorCapabilityId, EditorModuleInstanceId> candidateProviders,
        ICollection<string> diagnostics)
    {
        foreach (var definition in definitions.Values)
        {
            foreach (var capability in definition.Declaration.RequiredCapabilities)
            {
                var available = candidateProviders.ContainsKey(capability)
                    || (visibleApplication?.CapabilityProviders.ContainsKey(capability) ?? false);
                if (!available)
                {
                    diagnostics.Add(
                        $"Required capability '{capability}' for '{definition.Id}' has no provider.");
                }
            }
        }
    }

    private static IReadOnlyDictionary<TKey, TValue> ReadOnly<TKey, TValue>(
        IDictionary<TKey, TValue> values)
        where TKey : notnull
    {
        return new ReadOnlyDictionary<TKey, TValue>(
            new Dictionary<TKey, TValue>(values));
    }
}

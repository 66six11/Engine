using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodePinnedModuleScopeDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedModuleScopePreparation
{
    internal ProjectCodePinnedModuleScopePreparation(
        ProjectCodePinnedModuleDefinitionSet definitionSet,
        ScopeInstanceId scopeInstanceId,
        IReadOnlyList<EditorCapabilityId> hostCapabilities,
        EditorScopeTransaction transaction)
    {
        ArgumentNullException.ThrowIfNull(definitionSet);
        if (!scopeInstanceId.IsValid
            || scopeInstanceId == ScopeInstanceId.Application)
        {
            throw new ArgumentException(
                "Pinned module scope preparation requires a Project session scope.",
                nameof(scopeInstanceId));
        }

        ArgumentNullException.ThrowIfNull(hostCapabilities);
        ArgumentNullException.ThrowIfNull(transaction);
        var capabilityArray = hostCapabilities.ToArray();
        var candidate = transaction.Candidate;
        if (candidate.ScopeInstanceId != scopeInstanceId
            || candidate.RegistrationOrder.Count
                != definitionSet.Definitions.Count
            || candidate.Instances.Count
                != definitionSet.Definitions.Count
            || !candidate.HostCapabilities.SetEquals(capabilityArray))
        {
            throw new ArgumentException(
                "Prepared candidate does not match the exact Project scope input.",
                nameof(transaction));
        }

        for (var index = 0;
             index < definitionSet.Definitions.Count;
             ++index)
        {
            var definition = definitionSet.Definitions[index];
            var instance = candidate.RegistrationOrder[index];
            if (!ReferenceEquals(instance.Definition, definition)
                || !candidate.Instances.TryGetValue(
                    definition.Id,
                    out var keyedInstance)
                || !ReferenceEquals(instance, keyedInstance))
            {
                throw new ArgumentException(
                    "Prepared instance order differs from the exact definition set.",
                    nameof(transaction));
            }
        }

        DefinitionSet = definitionSet;
        ScopeInstanceId = scopeInstanceId;
        HostCapabilities = Array.AsReadOnly(capabilityArray);
        Transaction = transaction;
    }

    public ProjectCodePinnedModuleDefinitionSet DefinitionSet
    {
        get;
    }

    public ScopeInstanceId ScopeInstanceId { get; }

    public IReadOnlyList<EditorCapabilityId> HostCapabilities
    {
        get;
    }

    public EditorScopeTransaction Transaction { get; }

    public EditorScopePartition Candidate => Transaction.Candidate;
}

internal sealed class ProjectCodePinnedModuleScopePreparationResult
{
    private ProjectCodePinnedModuleScopePreparationResult(
        ProjectCodePinnedModuleScopePreparation? preparation,
        IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
            diagnostics)
    {
        Preparation = preparation;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleScopePreparation? Preparation
    {
        get;
    }

    public IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
        Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        Preparation is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleScopePreparationResult
        Success(ProjectCodePinnedModuleScopePreparation preparation)
    {
        ArgumentNullException.ThrowIfNull(preparation);
        return new(preparation, []);
    }

    internal static ProjectCodePinnedModuleScopePreparationResult
        Failure(IEnumerable<ProjectCodePinnedModuleScopeDiagnostic>
            diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        var snapshot = diagnostics
            .Distinct()
            .OrderBy(item => item.Location, StringComparer.Ordinal)
            .ThenBy(item => item.Code, StringComparer.Ordinal)
            .ThenBy(item => item.Message, StringComparer.Ordinal)
            .ToArray();
        if (snapshot.Length == 0)
        {
            throw new ArgumentException(
                "Failed scope preparation requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}

internal static class ProjectCodePinnedModuleScopePreparer
{
    public static ProjectCodePinnedModuleScopePreparationResult Prepare(
        ProjectCodePinnedModuleDefinitionSet definitionSet,
        ScopeInstanceId scopeInstanceId,
        EditorModuleRegistry registry,
        IEnumerable<EditorCapabilityId>? hostCapabilities = null)
    {
        ArgumentNullException.ThrowIfNull(definitionSet);
        if (!scopeInstanceId.IsValid
            || scopeInstanceId == ScopeInstanceId.Application)
        {
            throw new ArgumentException(
                "Pinned module scope preparation requires a Project session scope.",
                nameof(scopeInstanceId));
        }

        ArgumentNullException.ThrowIfNull(registry);
        var capabilitySnapshot =
            hostCapabilities?.ToArray() ?? [];
        try
        {
            var transaction = EditorScopeTransaction.Prepare(
                registry,
                scopeInstanceId,
                definitionSet.Definitions,
                capabilitySnapshot);
            return ProjectCodePinnedModuleScopePreparationResult.Success(
                new ProjectCodePinnedModuleScopePreparation(
                    definitionSet,
                    scopeInstanceId,
                    capabilitySnapshot,
                    transaction));
        }
        catch (EditorScopeValidationException error)
        {
            return ProjectCodePinnedModuleScopePreparationResult.Failure(
                error.Diagnostics.Select(message =>
                    new ProjectCodePinnedModuleScopeDiagnostic(
                        "project-code.pinned-module-scope-preparation.validation-failed",
                        "project-scope",
                        message)));
        }
    }
}

using System;
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Services;

public sealed class EditorContributionDescriptorValidator
{
    public EditorContributionValidationResult Validate(
        EditorContributionDescriptorSet descriptorSet,
        EditorContributionValidationContext? context = null)
    {
        ArgumentNullException.ThrowIfNull(descriptorSet);
        context ??= EditorContributionValidationContext.Empty;

        var errors = new List<EditorContributionValidationError>();
        var sourceId = descriptorSet.SourceId?.Value ?? string.Empty;
        var registeredPanelIds = CreateRegisteredIdSet(
            context.RegisteredPanelIds,
            nameof(EditorContributionValidationContext.RegisteredPanelIds));
        var registeredActionIds = CreateRegisteredIdSet(
            context.RegisteredActionIds,
            nameof(EditorContributionValidationContext.RegisteredActionIds));
        var registeredDiagnosticSourceIds = CreateRegisteredIdSet(
            context.RegisteredDiagnosticSourceIds,
            nameof(EditorContributionValidationContext.RegisteredDiagnosticSourceIds));

        if (string.IsNullOrWhiteSpace(sourceId))
        {
            AddError(errors, sourceId, string.Empty, "SourceId", "Source id must not be empty.");
        }

        if (!Enum.IsDefined(descriptorSet.SourceKind))
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "SourceKind",
                $"Source kind '{descriptorSet.SourceKind}' is not defined.");
        }

        var contributionOwners = new Dictionary<string, string>(StringComparer.Ordinal);

        if (descriptorSet.Panels is null)
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "Panels",
                "Panel descriptor collection must not be null.");
        }
        else
        {
            foreach (var panel in descriptorSet.Panels)
            {
                if (panel is null)
                {
                    AddError(errors, sourceId, string.Empty, "Panels", "Panel descriptor must not be null.");
                    continue;
                }

                ValidateContributionId(
                    errors,
                    sourceId,
                    panel.Id,
                    "Panel",
                    contributionOwners,
                    registeredPanelIds,
                    "Panel");
            }
        }

        if (descriptorSet.Actions is null)
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "Actions",
                "Action descriptor collection must not be null.");
        }
        else
        {
            foreach (var action in descriptorSet.Actions)
            {
                if (action is null)
                {
                    AddError(errors, sourceId, string.Empty, "Actions", "Action descriptor must not be null.");
                    continue;
                }

                ValidateContributionId(
                    errors,
                    sourceId,
                    action.Id,
                    "Action",
                    contributionOwners,
                    registeredActionIds,
                    "Action");
            }
        }

        if (descriptorSet.DiagnosticSources is null)
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "DiagnosticSources",
                "Diagnostic source descriptor collection must not be null.");
        }
        else
        {
            foreach (var diagnosticSource in descriptorSet.DiagnosticSources)
            {
                if (diagnosticSource is null)
                {
                    AddError(
                        errors,
                        sourceId,
                        string.Empty,
                        "DiagnosticSources",
                        "Diagnostic source descriptor must not be null.");
                    continue;
                }

                ValidateContributionId(
                    errors,
                    sourceId,
                    diagnosticSource.Id,
                    "DiagnosticSource",
                    contributionOwners,
                    registeredDiagnosticSourceIds,
                    "Diagnostic source");
            }
        }

        return errors.Count == 0
            ? EditorContributionValidationResult.Success
            : new EditorContributionValidationResult(errors.ToArray());
    }

    private static HashSet<string> CreateRegisteredIdSet(
        IReadOnlyCollection<string> ids,
        string paramName)
    {
        ArgumentNullException.ThrowIfNull(ids, paramName);

        return new HashSet<string>(ids, StringComparer.Ordinal);
    }

    private static void ValidateContributionId(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string id,
        string contributionType,
        Dictionary<string, string> contributionOwners,
        HashSet<string> registeredIds,
        string registeredName)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            AddError(
                errors,
                sourceId,
                string.Empty,
                "Id",
                $"{contributionType} id must not be empty.");
            return;
        }

        if (!contributionOwners.TryAdd(id, contributionType))
        {
            AddError(
                errors,
                sourceId,
                id,
                "Id",
                $"Contribution id '{id}' is already used by {contributionOwners[id]}.");
        }

        if (registeredIds.Contains(id))
        {
            AddError(
                errors,
                sourceId,
                id,
                "Id",
                $"{registeredName} id '{id}' is already registered.");
        }
    }

    private static void AddError(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string field,
        string message)
    {
        errors.Add(new EditorContributionValidationError(
            string.IsNullOrWhiteSpace(sourceId) ? string.Empty : sourceId,
            contributionId,
            field,
            message));
    }
}

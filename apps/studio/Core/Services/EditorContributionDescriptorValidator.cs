using System;
using System.Collections.Generic;
using Editor.Core.Models.Contributions;
using Asharia.Editor.Diagnostics;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.Panels;

namespace Editor.Core.Services;

// ReSharper disable ConditionIsAlwaysTrueOrFalseAccordingToNullableAPIContract
public sealed class EditorContributionDescriptorValidator
{
    public EditorContributionValidationResult Validate(
        EditorContributionDescriptorSet descriptorSet,
        EditorContributionValidationContext? context = null)
    {
        ArgumentNullException.ThrowIfNull(descriptorSet);
        context ??= EditorContributionValidationContext.Empty;

        var errors = new List<EditorContributionValidationError>();
        var sourceId = descriptorSet.SourceId.Value;
        var registeredPanelIds = CreateRegisteredIdSet(
            context.RegisteredPanelIds,
            nameof(EditorContributionValidationContext.RegisteredPanelIds));
        var registeredActionIds = CreateRegisteredIdSet(
            context.RegisteredActionIds,
            nameof(EditorContributionValidationContext.RegisteredActionIds));
        var registeredDiagnosticSourceIds = CreateRegisteredIdSet(
            context.RegisteredDiagnosticSourceIds,
            nameof(EditorContributionValidationContext.RegisteredDiagnosticSourceIds));
        var registeredContributionIds = new HashSet<string>(
            registeredPanelIds,
            StringComparer.Ordinal);
        registeredContributionIds.UnionWith(registeredActionIds);
        registeredContributionIds.UnionWith(registeredDiagnosticSourceIds);

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
        var panelIds = new List<string>(registeredPanelIds);
        var actionIds = new List<string>(registeredActionIds);
        var diagnosticSourceIds = new List<string>(registeredDiagnosticSourceIds);

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
                    registeredContributionIds,
                    "Panel");
                AddStableId(panelIds, panel.Id);
                ValidatePanel(errors, sourceId, panel);
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
                    registeredContributionIds,
                    "Action");
                AddStableId(actionIds, action.Id);
                ValidateAction(errors, sourceId, action);
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
                    registeredContributionIds,
                    "Diagnostic source");
                AddStableId(diagnosticSourceIds, diagnosticSource.Id);
                ValidateDiagnosticSource(errors, sourceId, diagnosticSource);
            }
        }

        ValidateDiagnosticSourceRoutePrefixes(
            errors,
            sourceId,
            diagnosticSourceIds,
            panelIds,
            actionIds);

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

    private static void AddStableId(List<string> ids, string id)
    {
        if (!string.IsNullOrWhiteSpace(id))
        {
            ids.Add(id);
        }
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

    private static void ValidatePanel(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorPanelContributionDescriptor panel)
    {
        ValidateDefinedEnum(errors, sourceId, panel.Id, "Kind", panel.Kind);
        ValidateDefinedEnum(errors, sourceId, panel.Id, "DefaultDockArea", panel.DefaultDockArea);
        ValidateMenuPath(errors, sourceId, panel.Id, panel.MenuPath);
        ValidateDefinedEnum(errors, sourceId, panel.Id, "CachePolicy", panel.CachePolicy);

        if (panel.ContentModel is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "ContentModel",
                "Panel content model must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "ContentModel.Kind",
                panel.ContentModel.Kind);

            if (string.IsNullOrWhiteSpace(panel.ContentModel.ModelId))
            {
                AddError(
                    errors,
                    sourceId,
                    panel.Id,
                    "ContentModel.ModelId",
                    "Panel content model id must not be empty.");
            }
        }

        if (panel.Lifecycle is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "Lifecycle",
                "Panel lifecycle descriptor must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "Lifecycle.Mode",
                panel.Lifecycle.Mode);
        }

        if (panel.FrameUpdate is null)
        {
            AddError(
                errors,
                sourceId,
                panel.Id,
                "FrameUpdate",
                "Panel frame update descriptor must not be null.");
        }
        else
        {
            ValidateDefinedEnum(
                errors,
                sourceId,
                panel.Id,
                "FrameUpdate.Mode",
                panel.FrameUpdate.Mode);

            if (panel.FrameUpdate.TargetFramesPerSecond is { } targetFramesPerSecond
                && (targetFramesPerSecond <= 0 || !double.IsFinite(targetFramesPerSecond)))
            {
                AddError(
                    errors,
                    sourceId,
                    panel.Id,
                    "FrameUpdate.TargetFramesPerSecond",
                    "Panel target frames per second must be finite and greater than zero.");
            }
        }
    }

    private static void ValidateDiagnosticSourceRoutePrefixes(
        List<EditorContributionValidationError> errors,
        string sourceId,
        IReadOnlyList<string> diagnosticSourceIds,
        IReadOnlyList<string> panelIds,
        IReadOnlyList<string> actionIds)
    {
        foreach (var diagnosticSourceId in diagnosticSourceIds)
        {
            foreach (var panelId in panelIds)
            {
                if (IsRoutePrefix(diagnosticSourceId, panelId))
                {
                    AddError(
                        errors,
                        sourceId,
                        diagnosticSourceId,
                        "Id",
                        $"Diagnostic source id '{diagnosticSourceId}' conflicts with panel id '{panelId}' route prefix.");
                }
            }

            foreach (var actionId in actionIds)
            {
                if (IsRoutePrefix(diagnosticSourceId, actionId))
                {
                    AddError(
                        errors,
                        sourceId,
                        diagnosticSourceId,
                        "Id",
                        $"Diagnostic source id '{diagnosticSourceId}' conflicts with action id '{actionId}' route prefix.");
                }
            }
        }
    }

    private static bool IsRoutePrefix(string routePrefix, string route)
    {
        return route.Length > routePrefix.Length
            && route.StartsWith(routePrefix, StringComparison.Ordinal)
            && route[routePrefix.Length] == '.';
    }

    private static void ValidateAction(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorActionContributionDescriptor action)
    {
        ValidateDefinedEnum(errors, sourceId, action.Id, "Scope", action.Scope);
        ValidateMenuPath(errors, sourceId, action.Id, action.MenuPath);

        if (string.IsNullOrWhiteSpace(action.CommandId))
        {
            AddError(
                errors,
                sourceId,
                action.Id,
                "CommandId",
                "Action command id must not be empty.");
        }
    }

    private static void ValidateDiagnosticSource(
        List<EditorContributionValidationError> errors,
        string sourceId,
        EditorDiagnosticSourceDescriptor diagnosticSource)
    {
        ValidateDefinedEnum(
            errors,
            sourceId,
            diagnosticSource.Id,
            "DefaultChannel",
            diagnosticSource.DefaultChannel);
        ValidateDefinedEnum(
            errors,
            sourceId,
            diagnosticSource.Id,
            "SourceKind",
            diagnosticSource.SourceKind);
    }

    private static void ValidateMenuPath(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string menuPath)
    {
        if (string.IsNullOrWhiteSpace(menuPath))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                "MenuPath",
                "Menu path must not be empty.");
            return;
        }

        var segments = menuPath.Split('/');
        if (menuPath.Contains("\\", StringComparison.Ordinal)
            || menuPath.StartsWith("/", StringComparison.Ordinal)
            || menuPath.EndsWith("/", StringComparison.Ordinal)
            || segments.Length < 2
            || Array.Exists(segments, string.IsNullOrWhiteSpace))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                "MenuPath",
                $"Menu path '{menuPath}' must be a slash-separated route with at least two non-empty segments.");
        }
    }

    private static void ValidateDefinedEnum<TEnum>(
        List<EditorContributionValidationError> errors,
        string sourceId,
        string contributionId,
        string field,
        TEnum value)
        where TEnum : struct, Enum
    {
        if (!Enum.IsDefined(value))
        {
            AddError(
                errors,
                sourceId,
                contributionId,
                field,
                $"{field} value '{value}' is not defined.");
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
// ReSharper restore ConditionIsAlwaysTrueOrFalseAccordingToNullableAPIContract

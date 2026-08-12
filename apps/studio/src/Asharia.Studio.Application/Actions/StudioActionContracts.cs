using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Actions;

public readonly record struct StudioActionId
{
    private readonly string? value_;

    public StudioActionId(string value)
    {
        value_ = StudioActionContractValidation.CanonicalId(value, nameof(value));
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public override string ToString() => Value;
}

public readonly record struct StudioActionPlacementId
{
    private readonly string? value_;

    public StudioActionPlacementId(string value)
    {
        value_ = StudioActionContractValidation.CanonicalId(value, nameof(value));
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public override string ToString() => Value;
}

public readonly record struct StudioPresentationId
{
    private readonly string? value_;

    public StudioPresentationId(string value)
    {
        value_ = StudioActionContractValidation.StableId(value, nameof(value));
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public override string ToString() => Value;
}

public sealed record StudioActionDefinition
{
    public StudioActionDefinition(
        StudioActionId id,
        string label,
        string description,
        string category)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(id));
        }

        Id = id;
        Label = StudioActionContractValidation.RequiredText(label, nameof(label));
        Description = StudioActionContractValidation.RequiredText(
            description,
            nameof(description));
        Category = StudioActionContractValidation.RequiredText(category, nameof(category));
    }

    public StudioActionId Id { get; }

    public string Label { get; }

    public string Description { get; }

    public string Category { get; }
}

public enum StudioActionPlacementKind
{
    Menu,
    Toolbar,
    ContextMenu,
    Shortcut,
}

public enum StudioActionScope
{
    FocusedPanel,
    Document,
    Workspace,
    Global,
}

[Flags]
public enum StudioShortcutModifiers
{
    None = 0,
    Control = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
    Meta = 1 << 3,
}

public sealed record StudioShortcutChord
{
    private const StudioShortcutModifiers AllModifiers =
        StudioShortcutModifiers.Control |
        StudioShortcutModifiers.Shift |
        StudioShortcutModifiers.Alt |
        StudioShortcutModifiers.Meta;

    public StudioShortcutChord(string key, StudioShortcutModifiers modifiers)
    {
        var normalizedKey = StudioActionContractValidation.RequiredText(key, nameof(key));
        if (normalizedKey.Contains('+', StringComparison.Ordinal) ||
            normalizedKey.IndexOfAny([' ', '\t', '\r', '\n']) >= 0)
        {
            throw new ArgumentException(
                "Shortcut key must be a single UI-neutral key name.",
                nameof(key));
        }
        if ((modifiers & ~AllModifiers) != 0)
        {
            throw new ArgumentOutOfRangeException(nameof(modifiers), modifiers, null);
        }

        Key = normalizedKey.ToUpperInvariant();
        Modifiers = modifiers;
    }

    public string Key { get; }

    public StudioShortcutModifiers Modifiers { get; }
}

public sealed record StudioActionPlacement
{
    public StudioActionPlacement(
        StudioActionPlacementId id,
        StudioActionPlacementKind kind,
        string? path,
        string section,
        int order,
        StudioActionScope scope,
        StudioShortcutChord? shortcut = null)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Action placement id must be valid.", nameof(id));
        }
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (!Enum.IsDefined(scope))
        {
            throw new ArgumentOutOfRangeException(nameof(scope), scope, null);
        }
        if ((kind == StudioActionPlacementKind.Shortcut) != (shortcut is not null))
        {
            throw new ArgumentException(
                "Exactly Shortcut placements must provide a shortcut chord.",
                nameof(shortcut));
        }
        if (kind == StudioActionPlacementKind.Shortcut)
        {
            if (!string.IsNullOrWhiteSpace(path))
            {
                throw new ArgumentException(
                    "Shortcut placements do not have a presentation path.",
                    nameof(path));
            }
        }
        else
        {
            path = StudioActionContractValidation.RequiredText(path, nameof(path));
        }

        Id = id;
        Kind = kind;
        Path = path;
        Section = StudioActionContractValidation.RequiredText(section, nameof(section));
        Order = order;
        Scope = scope;
        Shortcut = shortcut;
    }

    public StudioActionPlacementId Id { get; }

    public StudioActionPlacementKind Kind { get; }

    public string? Path { get; }

    public string Section { get; }

    public int Order { get; }

    public StudioActionScope Scope { get; }

    public StudioShortcutChord? Shortcut { get; }
}

public sealed record StudioActionCatalogEntry
{
    internal StudioActionCatalogEntry(
        StudioActionDefinition definition,
        ImmutableArray<StudioActionPlacement> placements)
    {
        Definition = definition;
        Placements = placements;
    }

    public StudioActionDefinition Definition { get; }

    public ImmutableArray<StudioActionPlacement> Placements { get; }
}

public sealed record StudioActionSelectionSnapshot
{
    public StudioActionSelectionSnapshot(
        IEnumerable<Guid> objectIds,
        Guid? primaryObjectId)
    {
        ArgumentNullException.ThrowIfNull(objectIds);

        var builder = ImmutableArray.CreateBuilder<Guid>();
        var uniqueIds = new HashSet<Guid>();
        foreach (var objectId in objectIds)
        {
            if (objectId == Guid.Empty)
            {
                throw new ArgumentException(
                    "Selection object ids must not be empty.",
                    nameof(objectIds));
            }
            if (!uniqueIds.Add(objectId))
            {
                throw new ArgumentException(
                    "Selection object ids must be unique.",
                    nameof(objectIds));
            }

            builder.Add(objectId);
        }

        if (primaryObjectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Primary selection object id must not be empty.",
                nameof(primaryObjectId));
        }
        if (builder.Count == 0 && primaryObjectId is not null)
        {
            throw new ArgumentException(
                "An empty selection cannot have a primary object.",
                nameof(primaryObjectId));
        }
        if (builder.Count > 0 &&
            (primaryObjectId is null || !uniqueIds.Contains(primaryObjectId.Value)))
        {
            throw new ArgumentException(
                "A non-empty selection requires a primary object from that selection.",
                nameof(primaryObjectId));
        }

        ObjectIds = builder.ToImmutable();
        PrimaryObjectId = primaryObjectId;
    }

    public static StudioActionSelectionSnapshot Empty { get; } = new([], null);

    public ImmutableArray<Guid> ObjectIds { get; }

    public Guid? PrimaryObjectId { get; }
}

public enum StudioActionTargetKind
{
    None,
    ProjectSession,
    Scene,
    SceneObject,
    Panel,
}

public sealed record StudioActionTarget
{
    private StudioActionTarget(
        StudioActionTargetKind kind,
        ProjectSessionId? projectSessionId,
        Guid? sceneId,
        Guid? objectId,
        StudioPresentationId? panelId)
    {
        Kind = kind;
        ProjectSessionId = projectSessionId;
        SceneId = sceneId;
        ObjectId = objectId;
        PanelId = panelId;
    }

    public static StudioActionTarget None { get; } = new(
        StudioActionTargetKind.None,
        projectSessionId: null,
        sceneId: null,
        objectId: null,
        panelId: null);

    public StudioActionTargetKind Kind { get; }

    public ProjectSessionId? ProjectSessionId { get; }

    public Guid? SceneId { get; }

    public Guid? ObjectId { get; }

    public StudioPresentationId? PanelId { get; }

    public static StudioActionTarget Project(ProjectSessionId projectSessionId)
    {
        ValidateProjectSessionId(projectSessionId);
        return new StudioActionTarget(
            StudioActionTargetKind.ProjectSession,
            projectSessionId,
            sceneId: null,
            objectId: null,
            panelId: null);
    }

    public static StudioActionTarget Scene(
        ProjectSessionId projectSessionId,
        Guid sceneId)
    {
        ValidateProjectSessionId(projectSessionId);
        ValidateGuid(sceneId, nameof(sceneId));
        return new StudioActionTarget(
            StudioActionTargetKind.Scene,
            projectSessionId,
            sceneId,
            objectId: null,
            panelId: null);
    }

    public static StudioActionTarget SceneObject(
        ProjectSessionId projectSessionId,
        Guid sceneId,
        Guid objectId)
    {
        ValidateProjectSessionId(projectSessionId);
        ValidateGuid(sceneId, nameof(sceneId));
        ValidateGuid(objectId, nameof(objectId));
        return new StudioActionTarget(
            StudioActionTargetKind.SceneObject,
            projectSessionId,
            sceneId,
            objectId,
            panelId: null);
    }

    public static StudioActionTarget Panel(StudioPresentationId panelId)
    {
        if (!panelId.IsValid)
        {
            throw new ArgumentException("Panel id must be valid.", nameof(panelId));
        }

        return new StudioActionTarget(
            StudioActionTargetKind.Panel,
            projectSessionId: null,
            sceneId: null,
            objectId: null,
            panelId);
    }

    private static void ValidateProjectSessionId(ProjectSessionId projectSessionId)
    {
        if (!projectSessionId.IsValid)
        {
            throw new ArgumentException(
                "Project session id must be valid.",
                nameof(projectSessionId));
        }
    }

    private static void ValidateGuid(Guid value, string parameterName)
    {
        if (value == Guid.Empty)
        {
            throw new ArgumentException("Stable id must not be empty.", parameterName);
        }
    }
}

public enum StudioActionInvocationSource
{
    Menu,
    Toolbar,
    ContextMenu,
    Shortcut,
    Programmatic,
}

public sealed record StudioActionContextSnapshot
{
    public StudioActionContextSnapshot(
        StudioActionInvocationSource source,
        StudioPresentationId? topLevelId,
        StudioPresentationId? focusedPanelId,
        ProjectSessionId? projectSessionId,
        Guid? sceneId,
        ulong? documentRevision,
        StudioActionSelectionSnapshot? selection,
        StudioActionTarget? target,
        Guid operationId,
        Guid correlationId,
        Guid? parentCorrelationId = null)
    {
        if (!Enum.IsDefined(source))
        {
            throw new ArgumentOutOfRangeException(nameof(source), source, null);
        }
        if (topLevelId is StudioPresentationId topLevel && !topLevel.IsValid)
        {
            throw new ArgumentException("Top-level id must be valid.", nameof(topLevelId));
        }
        if (focusedPanelId is StudioPresentationId focusedPanel && !focusedPanel.IsValid)
        {
            throw new ArgumentException(
                "Focused panel id must be valid.",
                nameof(focusedPanelId));
        }
        if (source != StudioActionInvocationSource.Programmatic && topLevelId is null)
        {
            throw new ArgumentException(
                "A UI action invocation requires a stable top-level id.",
                nameof(topLevelId));
        }
        if (focusedPanelId is not null && topLevelId is null)
        {
            throw new ArgumentException(
                "A focused panel requires a containing top-level id.",
                nameof(focusedPanelId));
        }
        if (projectSessionId is ProjectSessionId sessionId && !sessionId.IsValid)
        {
            throw new ArgumentException(
                "Project session id must be valid.",
                nameof(projectSessionId));
        }
        if (sceneId == Guid.Empty)
        {
            throw new ArgumentException("Scene id must not be empty.", nameof(sceneId));
        }
        if (documentRevision == 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(documentRevision),
                "Document revision must be non-zero.");
        }
        if ((sceneId is null) != (documentRevision is null))
        {
            throw new ArgumentException(
                "Scene id and document revision must be present together.",
                nameof(documentRevision));
        }
        if (sceneId is not null && projectSessionId is null)
        {
            throw new ArgumentException(
                "A scene context requires an active project session.",
                nameof(sceneId));
        }
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException("Operation id must not be empty.", nameof(operationId));
        }
        if (correlationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Correlation id must not be empty.",
                nameof(correlationId));
        }
        if (parentCorrelationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Parent correlation id must not be empty.",
                nameof(parentCorrelationId));
        }

        var frozenSelection = selection ?? StudioActionSelectionSnapshot.Empty;
        if (!frozenSelection.ObjectIds.IsEmpty && sceneId is null)
        {
            throw new ArgumentException(
                "A scene selection requires an active scene context.",
                nameof(selection));
        }

        var frozenTarget = target ?? StudioActionTarget.None;
        if (source == StudioActionInvocationSource.ContextMenu &&
            frozenTarget.Kind == StudioActionTargetKind.None)
        {
            throw new ArgumentException(
                "A context-menu invocation requires an explicit frozen target.",
                nameof(target));
        }
        if (frozenTarget.ProjectSessionId is ProjectSessionId targetSessionId &&
            targetSessionId != projectSessionId)
        {
            throw new ArgumentException(
                "Action target project session must match the active context.",
                nameof(target));
        }
        if (frozenTarget.SceneId is Guid targetSceneId && targetSceneId != sceneId)
        {
            throw new ArgumentException(
                "Action target scene must match the active context.",
                nameof(target));
        }

        Source = source;
        TopLevelId = topLevelId;
        FocusedPanelId = focusedPanelId;
        ProjectSessionId = projectSessionId;
        SceneId = sceneId;
        DocumentRevision = documentRevision;
        Selection = frozenSelection;
        Target = frozenTarget;
        OperationId = operationId;
        CorrelationId = correlationId;
        ParentCorrelationId = parentCorrelationId;
    }

    public StudioActionInvocationSource Source { get; }

    public StudioPresentationId? TopLevelId { get; }

    public StudioPresentationId? FocusedPanelId { get; }

    public ProjectSessionId? ProjectSessionId { get; }

    public Guid? SceneId { get; }

    public ulong? DocumentRevision { get; }

    public StudioActionSelectionSnapshot Selection { get; }

    public StudioActionTarget Target { get; }

    public Guid OperationId { get; }

    public Guid CorrelationId { get; }

    public Guid? ParentCorrelationId { get; }
}

public enum StudioActionBlockKind
{
    None,
    Disabled,
    Stale,
    Conflict,
}

public enum StudioActionCheckState
{
    NotCheckable,
    Unchecked,
    Checked,
}

public sealed record StudioActionState
{
    public StudioActionState(
        bool isVisible,
        StudioActionBlockKind blockKind,
        StudioActionCheckState checkState,
        bool isRunning,
        string? disabledReason = null,
        string? presentationLabel = null)
    {
        if (!Enum.IsDefined(blockKind))
        {
            throw new ArgumentOutOfRangeException(nameof(blockKind), blockKind, null);
        }
        if (!Enum.IsDefined(checkState))
        {
            throw new ArgumentOutOfRangeException(nameof(checkState), checkState, null);
        }
        if ((blockKind == StudioActionBlockKind.None) !=
            string.IsNullOrWhiteSpace(disabledReason))
        {
            throw new ArgumentException(
                "A blocked action requires a reason and an available action cannot have one.",
                nameof(disabledReason));
        }

        IsVisible = isVisible;
        BlockKind = blockKind;
        CheckState = checkState;
        IsRunning = isRunning;
        DisabledReason = disabledReason;
        PresentationLabel = presentationLabel is null
            ? null
            : StudioActionContractValidation.RequiredText(
                presentationLabel,
                nameof(presentationLabel));
    }

    public bool IsVisible { get; }

    public bool IsEnabled => BlockKind == StudioActionBlockKind.None;

    public StudioActionBlockKind BlockKind { get; }

    public StudioActionCheckState CheckState { get; }

    public bool? IsChecked => CheckState switch
    {
        StudioActionCheckState.Checked => true,
        StudioActionCheckState.Unchecked => false,
        _ => null,
    };

    public bool IsRunning { get; }

    public string? DisabledReason { get; }

    public string? PresentationLabel { get; }

    public static StudioActionState Available(
        bool isVisible = true,
        StudioActionCheckState checkState = StudioActionCheckState.NotCheckable,
        bool isRunning = false,
        string? presentationLabel = null) =>
        new(
            isVisible,
            StudioActionBlockKind.None,
            checkState,
            isRunning,
            presentationLabel: presentationLabel);

    public static StudioActionState Blocked(
        StudioActionBlockKind blockKind,
        string reason,
        bool isVisible = true,
        StudioActionCheckState checkState = StudioActionCheckState.NotCheckable,
        bool isRunning = false,
        string? presentationLabel = null) =>
        new(
            isVisible,
            blockKind,
            checkState,
            isRunning,
            reason,
            presentationLabel);
}

public enum StudioActionResultStatus
{
    Succeeded,
    Unknown,
    Disabled,
    Stale,
    Conflict,
    Cancelled,
    Failed,
}

public sealed record StudioActionCompletion
{
    private StudioActionCompletion(
        StudioActionResultStatus status,
        string message,
        long? diagnosticSequence,
        ProjectEditId? projectEditId)
    {
        if (status == StudioActionResultStatus.Unknown || !Enum.IsDefined(status))
        {
            throw new ArgumentOutOfRangeException(nameof(status), status, null);
        }
        if (diagnosticSequence <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(diagnosticSequence),
                "A diagnostic sequence must be positive.");
        }
        if (projectEditId is ProjectEditId editId && !editId.IsValid)
        {
            throw new ArgumentException(
                "Project edit id must be valid.",
                nameof(projectEditId));
        }

        Status = status;
        Message = StudioActionContractValidation.RequiredText(message, nameof(message));
        DiagnosticSequence = diagnosticSequence;
        ProjectEditId = projectEditId;
    }

    public StudioActionResultStatus Status { get; }

    public string Message { get; }

    public long? DiagnosticSequence { get; }

    public ProjectEditId? ProjectEditId { get; }

    public static StudioActionCompletion Succeeded(
        string message,
        long? diagnosticSequence = null,
        ProjectEditId? projectEditId = null) =>
        new(
            StudioActionResultStatus.Succeeded,
            message,
            diagnosticSequence,
            projectEditId);

    public static StudioActionCompletion Disabled(
        string message,
        long? diagnosticSequence = null) =>
        new(StudioActionResultStatus.Disabled, message, diagnosticSequence, null);

    public static StudioActionCompletion Stale(
        string message,
        long? diagnosticSequence = null) =>
        new(StudioActionResultStatus.Stale, message, diagnosticSequence, null);

    public static StudioActionCompletion Conflict(
        string message,
        long? diagnosticSequence = null) =>
        new(StudioActionResultStatus.Conflict, message, diagnosticSequence, null);

    public static StudioActionCompletion Cancelled(string message) =>
        new(StudioActionResultStatus.Cancelled, message, null, null);

    public static StudioActionCompletion Failed(
        string message,
        long? diagnosticSequence = null) =>
        new(StudioActionResultStatus.Failed, message, diagnosticSequence, null);
}

public sealed record StudioActionResult
{
    internal StudioActionResult(
        StudioActionId actionId,
        StudioActionResultStatus status,
        Guid operationId,
        Guid correlationId,
        Guid? parentCorrelationId,
        string message,
        long? diagnosticSequence,
        ProjectEditId? projectEditId)
    {
        ActionId = actionId;
        Status = status;
        OperationId = operationId;
        CorrelationId = correlationId;
        ParentCorrelationId = parentCorrelationId;
        Message = message;
        DiagnosticSequence = diagnosticSequence;
        ProjectEditId = projectEditId;
    }

    public StudioActionId ActionId { get; }

    public StudioActionResultStatus Status { get; }

    public Guid OperationId { get; }

    public Guid CorrelationId { get; }

    public Guid? ParentCorrelationId { get; }

    public string Message { get; }

    public long? DiagnosticSequence { get; }

    public ProjectEditId? ProjectEditId { get; }

    public StudioActionResult WithDiagnosticSequence(long diagnosticSequence)
    {
        if (diagnosticSequence <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(diagnosticSequence),
                "A diagnostic sequence must be positive.");
        }

        return new StudioActionResult(
            ActionId,
            Status,
            OperationId,
            CorrelationId,
            ParentCorrelationId,
            Message,
            diagnosticSequence,
            ProjectEditId);
    }
}

public enum StudioActionStateEvaluationStatus
{
    Evaluated,
    UnknownAction,
    Failed,
}

public sealed record StudioActionStateEvaluation
{
    private StudioActionStateEvaluation(
        StudioActionId actionId,
        StudioActionStateEvaluationStatus status,
        StudioActionState? state,
        string? failureMessage)
    {
        ActionId = actionId;
        Status = status;
        State = state;
        FailureMessage = failureMessage;
    }

    public StudioActionId ActionId { get; }

    public StudioActionStateEvaluationStatus Status { get; }

    public StudioActionState? State { get; }

    public string? FailureMessage { get; }

    internal static StudioActionStateEvaluation Evaluated(
        StudioActionId actionId,
        StudioActionState state) =>
        new(actionId, StudioActionStateEvaluationStatus.Evaluated, state, null);

    internal static StudioActionStateEvaluation Unknown(StudioActionId actionId) =>
        new(
            actionId,
            StudioActionStateEvaluationStatus.UnknownAction,
            state: null,
            $"Action '{actionId}' is not registered.");

    internal static StudioActionStateEvaluation Failed(
        StudioActionId actionId,
        string message) =>
        new(
            actionId,
            StudioActionStateEvaluationStatus.Failed,
            state: null,
            StudioActionContractValidation.RequiredText(message, nameof(message)));
}

public delegate StudioActionState StudioActionStateEvaluator(
    StudioActionContextSnapshot context);

public delegate ValueTask<StudioActionCompletion> StudioActionHandler(
    StudioActionContextSnapshot context,
    CancellationToken cancellationToken);

internal static class StudioActionContractValidation
{
    private const int MaxIdentifierLength = 128;

    public static string CanonicalId(string value, string parameterName)
    {
        var id = StableId(value, parameterName);
        if (id != id.ToLowerInvariant())
        {
            throw new ArgumentException(
                "Stable action ids must use lowercase canonical form.",
                parameterName);
        }

        foreach (var character in id)
        {
            if ((character < 'a' || character > 'z') &&
                (character < '0' || character > '9') &&
                character is not '.' and not '-' and not '_')
            {
                throw new ArgumentException(
                    "Stable action ids may contain lowercase ASCII letters, digits, '.', '-' and '_'.",
                    parameterName);
            }
        }

        return id;
    }

    public static string StableId(string value, string parameterName)
    {
        ArgumentNullException.ThrowIfNull(value, parameterName);
        if (string.IsNullOrWhiteSpace(value) || value != value.Trim() ||
            value.Length > MaxIdentifierLength)
        {
            throw new ArgumentException(
                $"Stable id must contain 1-{MaxIdentifierLength} non-padding characters.",
                parameterName);
        }
        foreach (var character in value)
        {
            if (char.IsControl(character))
            {
                throw new ArgumentException(
                    "Stable id must not contain control characters.",
                    parameterName);
            }
        }

        return value;
    }

    public static string RequiredText(string? value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Value must not be empty.", parameterName);
        }

        return value;
    }
}

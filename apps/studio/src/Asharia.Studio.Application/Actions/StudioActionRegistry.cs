using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;

namespace Asharia.Studio.Application.Actions;

public enum StudioActionRegistrationFailureKind
{
    DuplicateActionId,
    DuplicatePlacementId,
    ShortcutCollision,
}

public sealed class StudioActionRegistrationException : Exception
{
    internal StudioActionRegistrationException(
        StudioActionRegistrationFailureKind kind,
        StudioActionId actionId,
        StudioActionId conflictingActionId,
        string message,
        StudioActionPlacementId? placementId = null,
        StudioShortcutChord? shortcut = null) : base(message)
    {
        Kind = kind;
        ActionId = actionId;
        ConflictingActionId = conflictingActionId;
        PlacementId = placementId;
        Shortcut = shortcut;
    }

    public StudioActionRegistrationFailureKind Kind { get; }

    public StudioActionId ActionId { get; }

    public StudioActionId ConflictingActionId { get; }

    public StudioActionPlacementId? PlacementId { get; }

    public StudioShortcutChord? Shortcut { get; }
}

public sealed class StudioActionRegistry
{
    private readonly object gate_ = new();
    private readonly Dictionary<StudioActionId, RuntimeEntry> entries_ = [];
    private readonly Dictionary<StudioActionPlacementId, StudioActionId> placementOwners_ = [];
    private readonly Dictionary<StudioShortcutChord, StudioActionId> shortcutOwners_ = [];
    private ImmutableArray<StudioActionCatalogEntry> catalog_ = [];

    public void Register(
        StudioActionDefinition definition,
        IEnumerable<StudioActionPlacement> placements,
        StudioActionStateEvaluator stateEvaluator,
        StudioActionHandler handler)
    {
        ArgumentNullException.ThrowIfNull(definition);
        ArgumentNullException.ThrowIfNull(placements);
        ArgumentNullException.ThrowIfNull(stateEvaluator);
        ArgumentNullException.ThrowIfNull(handler);

        var frozenPlacements = placements.ToImmutableArray();
        if (frozenPlacements.IsEmpty)
        {
            throw new ArgumentException(
                "A registered action requires at least one real placement.",
                nameof(placements));
        }
        if (frozenPlacements.Any(static placement => placement is null))
        {
            throw new ArgumentException(
                "Action placements must not contain null values.",
                nameof(placements));
        }

        var localPlacementIds = new HashSet<StudioActionPlacementId>();
        var localShortcuts = new HashSet<StudioShortcutChord>();
        foreach (var placement in frozenPlacements)
        {
            if (!localPlacementIds.Add(placement.Id))
            {
                throw DuplicatePlacement(
                    definition.Id,
                    definition.Id,
                    placement.Id);
            }
            if (placement.Shortcut is StudioShortcutChord shortcut &&
                !localShortcuts.Add(shortcut))
            {
                throw ShortcutCollision(
                    definition.Id,
                    definition.Id,
                    shortcut);
            }
        }

        var catalogEntry = new StudioActionCatalogEntry(definition, frozenPlacements);
        var runtimeEntry = new RuntimeEntry(catalogEntry, stateEvaluator, handler);

        lock (gate_)
        {
            if (entries_.ContainsKey(definition.Id))
            {
                throw new StudioActionRegistrationException(
                    StudioActionRegistrationFailureKind.DuplicateActionId,
                    definition.Id,
                    definition.Id,
                    $"Action id '{definition.Id}' is already registered.");
            }

            foreach (var placement in frozenPlacements)
            {
                if (placementOwners_.TryGetValue(
                    placement.Id,
                    out var conflictingActionId))
                {
                    throw DuplicatePlacement(
                        definition.Id,
                        conflictingActionId,
                        placement.Id);
                }
                if (placement.Shortcut is StudioShortcutChord shortcut &&
                    shortcutOwners_.TryGetValue(shortcut, out conflictingActionId))
                {
                    throw ShortcutCollision(
                        definition.Id,
                        conflictingActionId,
                        shortcut);
                }
            }

            entries_.Add(definition.Id, runtimeEntry);
            foreach (var placement in frozenPlacements)
            {
                placementOwners_.Add(placement.Id, definition.Id);
                if (placement.Shortcut is StudioShortcutChord shortcut)
                {
                    shortcutOwners_.Add(shortcut, definition.Id);
                }
            }
            catalog_ = catalog_.Add(catalogEntry);
        }
    }

    public ImmutableArray<StudioActionCatalogEntry> GetActions()
    {
        lock (gate_)
        {
            return catalog_;
        }
    }

    public bool TryGetAction(
        StudioActionId actionId,
        out StudioActionCatalogEntry? action)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }

        lock (gate_)
        {
            if (entries_.TryGetValue(actionId, out var entry))
            {
                action = entry.CatalogEntry;
                return true;
            }
        }

        action = null;
        return false;
    }

    public bool TryResolveShortcut(
        StudioShortcutChord shortcut,
        out StudioActionId actionId)
    {
        ArgumentNullException.ThrowIfNull(shortcut);
        lock (gate_)
        {
            return shortcutOwners_.TryGetValue(shortcut, out actionId);
        }
    }

    internal bool TryGetRuntimeEntry(
        StudioActionId actionId,
        out RuntimeEntry? entry)
    {
        lock (gate_)
        {
            return entries_.TryGetValue(actionId, out entry);
        }
    }

    private static StudioActionRegistrationException DuplicatePlacement(
        StudioActionId actionId,
        StudioActionId conflictingActionId,
        StudioActionPlacementId placementId) =>
        new(
            StudioActionRegistrationFailureKind.DuplicatePlacementId,
            actionId,
            conflictingActionId,
            $"Action placement id '{placementId}' is already registered by action " +
            $"'{conflictingActionId}'.",
            placementId);

    private static StudioActionRegistrationException ShortcutCollision(
        StudioActionId actionId,
        StudioActionId conflictingActionId,
        StudioShortcutChord shortcut) =>
        new(
            StudioActionRegistrationFailureKind.ShortcutCollision,
            actionId,
            conflictingActionId,
            $"Shortcut '{shortcut.Modifiers}+{shortcut.Key}' is already registered by action " +
            $"'{conflictingActionId}'.",
            shortcut: shortcut);

    internal sealed record RuntimeEntry(
        StudioActionCatalogEntry CatalogEntry,
        StudioActionStateEvaluator StateEvaluator,
        StudioActionHandler Handler);
}

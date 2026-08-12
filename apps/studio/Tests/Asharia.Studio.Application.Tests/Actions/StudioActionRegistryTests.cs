using System;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Studio.Application.Actions;
using Xunit;

namespace Asharia.Studio.Application.Tests.Actions;

public sealed class StudioActionRegistryTests
{
    [Fact]
    public void Catalog_is_immutable_and_preserves_registration_order()
    {
        var registry = new StudioActionRegistry();
        Register(registry, "studio.file.save", Menu("save-menu", "File/Save"));
        var firstSnapshot = registry.GetActions();
        Register(registry, "studio.edit.undo", Menu("undo-menu", "Edit/Undo"));

        Assert.Equal(
            ["studio.file.save"],
            firstSnapshot.Select(entry => entry.Definition.Id.Value));
        Assert.Equal(
            ["studio.file.save", "studio.edit.undo"],
            registry.GetActions().Select(entry => entry.Definition.Id.Value));
    }

    [Fact]
    public void Duplicate_action_id_fails_closed_without_partial_registration()
    {
        var registry = new StudioActionRegistry();
        Register(registry, "studio.file.save", Menu("save-menu", "File/Save"));

        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            Register(registry, "studio.file.save", Menu("save-toolbar", "Main/Save")));

        Assert.Equal(
            StudioActionRegistrationFailureKind.DuplicateActionId,
            exception.Kind);
        Assert.Single(registry.GetActions());
        Assert.False(registry.TryGetAction(
            new StudioActionId("studio.edit.undo"),
            out _));
    }

    [Fact]
    public void Duplicate_placement_id_fails_closed_without_leaking_other_indexes()
    {
        var registry = new StudioActionRegistry();
        Register(registry, "studio.file.save", Menu("main-menu-item", "File/Save"));

        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            Register(
                registry,
                "studio.edit.undo",
                Menu("undo-toolbar", "Main/Undo"),
                Menu("main-menu-item", "Edit/Undo")));

        Assert.Equal(
            StudioActionRegistrationFailureKind.DuplicatePlacementId,
            exception.Kind);
        Assert.Equal("studio.file.save", exception.ConflictingActionId.Value);
        Assert.False(registry.TryGetAction(
            new StudioActionId("studio.edit.undo"),
            out _));
    }

    [Fact]
    public void Shortcut_collision_fails_closed_and_never_chooses_a_winner_by_order()
    {
        var registry = new StudioActionRegistry();
        var chord = new StudioShortcutChord("S", StudioShortcutModifiers.Control);
        Register(registry, "studio.file.save", Shortcut("save-shortcut", chord));

        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            Register(
                registry,
                "studio.file.save-as",
                Shortcut("save-as-shortcut", chord)));

        Assert.Equal(
            StudioActionRegistrationFailureKind.ShortcutCollision,
            exception.Kind);
        Assert.Equal("studio.file.save", exception.ConflictingActionId.Value);
        Assert.True(registry.TryResolveShortcut(chord, out var actionId));
        Assert.Equal("studio.file.save", actionId.Value);
        Assert.False(registry.TryGetAction(
            new StudioActionId("studio.file.save-as"),
            out _));
    }

    [Fact]
    public void Same_shortcut_twice_in_one_registration_is_rejected()
    {
        var registry = new StudioActionRegistry();
        var chord = new StudioShortcutChord("Z", StudioShortcutModifiers.Control);

        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            Register(
                registry,
                "studio.edit.undo",
                Shortcut("undo-one", chord),
                Shortcut("undo-two", chord)));

        Assert.Equal(
            StudioActionRegistrationFailureKind.ShortcutCollision,
            exception.Kind);
        Assert.Empty(registry.GetActions());
    }

    [Fact]
    public void Duplicate_placement_inside_one_action_fails_before_registration()
    {
        var registry = new StudioActionRegistry();

        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            Register(
                registry,
                "studio.window.hierarchy",
                Menu("hierarchy-menu", "Window/Hierarchy"),
                Menu("hierarchy-menu", "Window/Panels/Hierarchy")));

        Assert.Equal(
            StudioActionRegistrationFailureKind.DuplicatePlacementId,
            exception.Kind);
        Assert.Empty(registry.GetActions());
    }

    private static void Register(
        StudioActionRegistry registry,
        string actionId,
        params StudioActionPlacement[] placements) =>
        registry.Register(
            new StudioActionDefinition(
                new StudioActionId(actionId),
                actionId,
                $"Execute {actionId}.",
                "Test"),
            placements,
            _ => StudioActionState.Available(),
            (_, _) => ValueTask.FromResult(
                StudioActionCompletion.Succeeded("Executed.")));

    private static StudioActionPlacement Menu(string id, string path) =>
        new(
            new StudioActionPlacementId(id),
            StudioActionPlacementKind.Menu,
            path,
            "test",
            order: 0,
            StudioActionScope.Workspace);

    private static StudioActionPlacement Shortcut(
        string id,
        StudioShortcutChord chord) =>
        new(
            new StudioActionPlacementId(id),
            StudioActionPlacementKind.Shortcut,
            path: null,
            "test",
            order: 0,
            StudioActionScope.Document,
            chord);
}

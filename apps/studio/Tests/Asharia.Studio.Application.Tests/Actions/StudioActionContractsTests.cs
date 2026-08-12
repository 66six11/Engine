using System;
using System.Linq;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Actions;

public sealed class StudioActionContractsTests
{
    [Fact]
    public void Stable_action_ids_require_canonical_lowercase_values()
    {
        Assert.Equal(
            "studio.file.save",
            new StudioActionId("studio.file.save").Value);
        Assert.Throws<ArgumentException>(() => new StudioActionId("Studio.File.Save"));
        Assert.Throws<ArgumentException>(() => new StudioActionId("studio/file/save"));
        Assert.Throws<ArgumentException>(() => new StudioActionId(" studio.file.save"));
        Assert.False(default(StudioActionId).IsValid);
    }

    [Fact]
    public void Selection_snapshot_is_immutable_and_requires_a_member_primary()
    {
        var first = Guid.NewGuid();
        var second = Guid.NewGuid();
        var source = new[] { first, second };

        var snapshot = new StudioActionSelectionSnapshot(source, second);
        source[0] = Guid.NewGuid();

        Assert.Equal([first, second], snapshot.ObjectIds.ToArray());
        Assert.Equal(second, snapshot.PrimaryObjectId);
        Assert.Throws<ArgumentException>(() =>
            new StudioActionSelectionSnapshot([first], second));
        Assert.Throws<ArgumentException>(() =>
            new StudioActionSelectionSnapshot([first, first], first));
    }

    [Fact]
    public void Context_menu_requires_an_explicit_target_in_the_same_scope()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var otherSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var objectId = Guid.NewGuid();

        var context = new StudioActionContextSnapshot(
            StudioActionInvocationSource.ContextMenu,
            new StudioPresentationId("main-window"),
            new StudioPresentationId("hierarchy"),
            sessionId,
            sceneId,
            documentRevision: 7,
            new StudioActionSelectionSnapshot([objectId], objectId),
            StudioActionTarget.SceneObject(sessionId, sceneId, objectId),
            Guid.NewGuid(),
            Guid.NewGuid());

        Assert.Equal(objectId, context.Target.ObjectId);
        Assert.Throws<ArgumentException>(() => new StudioActionContextSnapshot(
            StudioActionInvocationSource.ContextMenu,
            new StudioPresentationId("main-window"),
            new StudioPresentationId("hierarchy"),
            sessionId,
            sceneId,
            documentRevision: 7,
            StudioActionSelectionSnapshot.Empty,
            StudioActionTarget.None,
            Guid.NewGuid(),
            Guid.NewGuid()));
        Assert.Throws<ArgumentException>(() => new StudioActionContextSnapshot(
            StudioActionInvocationSource.ContextMenu,
            new StudioPresentationId("main-window"),
            new StudioPresentationId("hierarchy"),
            sessionId,
            sceneId,
            documentRevision: 7,
            StudioActionSelectionSnapshot.Empty,
            StudioActionTarget.Project(otherSessionId),
            Guid.NewGuid(),
            Guid.NewGuid()));
    }

    [Fact]
    public void State_requires_reason_exactly_when_blocked()
    {
        Assert.True(StudioActionState.Available().IsEnabled);
        Assert.Equal(
            "Undo Rename Entity",
            StudioActionState.Available(
                presentationLabel: "Undo Rename Entity").PresentationLabel);
        Assert.Throws<ArgumentException>(() => StudioActionState.Available(
            presentationLabel: " "));
        Assert.Throws<ArgumentException>(() => new StudioActionState(
            isVisible: true,
            StudioActionBlockKind.Disabled,
            StudioActionCheckState.NotCheckable,
            isRunning: false));
        Assert.Throws<ArgumentException>(() => new StudioActionState(
            isVisible: true,
            StudioActionBlockKind.None,
            StudioActionCheckState.NotCheckable,
            isRunning: false,
            "Unexpected reason."));
    }

    [Fact]
    public void Completion_requires_positive_diagnostic_sequence_and_valid_edit_id()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            StudioActionCompletion.Failed("Failed.", diagnosticSequence: 0));
        Assert.Throws<ArgumentException>(() =>
            StudioActionCompletion.Succeeded(
                "Succeeded.",
                projectEditId: default(ProjectEditId)));
    }

    [Fact]
    public void Placement_requires_exact_kind_specific_fields()
    {
        var shortcut = new StudioShortcutChord(
            "S",
            StudioShortcutModifiers.Control);

        Assert.Throws<ArgumentException>(() => new StudioActionPlacement(
            new StudioActionPlacementId("save-menu"),
            StudioActionPlacementKind.Menu,
            "File/Save",
            "file",
            order: 0,
            StudioActionScope.Workspace,
            shortcut));
        Assert.Throws<ArgumentException>(() => new StudioActionPlacement(
            new StudioActionPlacementId("save-shortcut"),
            StudioActionPlacementKind.Shortcut,
            "File/Save",
            "file",
            order: 0,
            StudioActionScope.Workspace,
            shortcut));
    }
}

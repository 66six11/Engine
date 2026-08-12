using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Actions;

public sealed class StudioActionExecutorTests
{
    [Fact]
    public async Task Execute_revalidates_state_after_a_menu_snapshot_was_computed()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.scene.rename");
        var expectedRevision = 7UL;
        var liveRevision = expectedRevision;
        var handlerCalls = 0;
        registry.Register(
            Definition(actionId),
            [Menu()],
            context => context.DocumentRevision == liveRevision
                ? StudioActionState.Available()
                : StudioActionState.Blocked(
                    StudioActionBlockKind.Stale,
                    "The scene revision changed."),
            (_, _) =>
            {
                handlerCalls++;
                return ValueTask.FromResult(
                    StudioActionCompletion.Succeeded("Renamed."));
            });
        var executor = new StudioActionExecutor(registry);
        var context = Context(expectedRevision);

        var projected = executor.EvaluateState(actionId, context);
        Assert.True(projected.State!.IsEnabled);
        liveRevision++;

        var result = await executor.ExecuteAsync(actionId, context);

        Assert.Equal(StudioActionResultStatus.Stale, result.Status);
        Assert.Equal(0, handlerCalls);
        Assert.Equal(context.OperationId, result.OperationId);
        Assert.Equal(context.CorrelationId, result.CorrelationId);
    }

    [Fact]
    public async Task Context_target_is_frozen_even_if_live_selection_changes()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.scene.delete-object");
        var frozenObjectId = Guid.NewGuid();
        var laterSelection = Guid.NewGuid();
        Guid? handledObjectId = null;
        registry.Register(
            Definition(actionId),
            [ContextMenu()],
            _ => StudioActionState.Available(),
            (context, _) =>
            {
                handledObjectId = context.Target.ObjectId;
                return ValueTask.FromResult(
                    StudioActionCompletion.Succeeded("Deleted."));
            });
        var executor = new StudioActionExecutor(registry);
        var context = Context(
            revision: 7,
            StudioActionInvocationSource.ContextMenu,
            frozenObjectId);

        _ = laterSelection;
        var result = await executor.ExecuteAsync(actionId, context);

        Assert.Equal(StudioActionResultStatus.Succeeded, result.Status);
        Assert.Equal(frozenObjectId, handledObjectId);
    }

    [Fact]
    public async Task Completion_preserves_diagnostic_edit_and_correlation_identity()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.edit.apply-transform");
        var editId = ProjectEditId.CreateNew();
        registry.Register(
            Definition(actionId),
            [Menu()],
            _ => StudioActionState.Available(),
            (_, _) => ValueTask.FromResult(
                StudioActionCompletion.Succeeded(
                    "Applied.",
                    diagnosticSequence: 42,
                    projectEditId: editId)));
        var executor = new StudioActionExecutor(registry);
        var context = Context(7);

        var result = await executor.ExecuteAsync(actionId, context);

        Assert.Equal(StudioActionResultStatus.Succeeded, result.Status);
        Assert.Equal(42, result.DiagnosticSequence);
        Assert.Equal(editId, result.ProjectEditId);
        Assert.Equal(context.OperationId, result.OperationId);
        Assert.Equal(context.CorrelationId, result.CorrelationId);
        Assert.Equal(context.ParentCorrelationId, result.ParentCorrelationId);
    }

    [Fact]
    public async Task Unknown_disabled_and_conflict_results_do_not_call_handlers()
    {
        var registry = new StudioActionRegistry();
        var disabledId = new StudioActionId("studio.file.save");
        var conflictId = new StudioActionId("studio.edit.undo");
        var handlerCalls = 0;
        RegisterBlocked(
            registry,
            disabledId,
            StudioActionBlockKind.Disabled,
            "No document is open.",
            () => handlerCalls++);
        RegisterBlocked(
            registry,
            conflictId,
            StudioActionBlockKind.Conflict,
            "The edit owner is busy.",
            () => handlerCalls++);
        var executor = new StudioActionExecutor(registry);
        var context = Context(7);

        var unknown = await executor.ExecuteAsync(
            new StudioActionId("studio.missing"),
            context);
        var disabled = await executor.ExecuteAsync(disabledId, context);
        var conflict = await executor.ExecuteAsync(conflictId, context);

        Assert.Equal(StudioActionResultStatus.Unknown, unknown.Status);
        Assert.Equal(StudioActionResultStatus.Disabled, disabled.Status);
        Assert.Equal(StudioActionResultStatus.Conflict, conflict.Status);
        Assert.Equal(0, handlerCalls);
    }

    [Fact]
    public async Task Cancellation_and_exceptions_become_typed_results()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.file.open");
        registry.Register(
            Definition(actionId),
            [Menu()],
            _ => StudioActionState.Available(),
            (_, _) => throw new InvalidOperationException("Open adapter failed."));
        var executor = new StudioActionExecutor(registry);
        var context = Context(7);

        var failed = await executor.ExecuteAsync(actionId, context);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        var cancelled = await executor.ExecuteAsync(
            actionId,
            context,
            cancellation.Token);

        Assert.Equal(StudioActionResultStatus.Failed, failed.Status);
        Assert.Contains("Open adapter failed", failed.Message, StringComparison.Ordinal);
        Assert.Equal(StudioActionResultStatus.Cancelled, cancelled.Status);
    }

    [Fact]
    public void State_projection_failure_is_typed_and_does_not_throw()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.window.panel");
        registry.Register(
            Definition(actionId),
            [Menu()],
            _ => throw new InvalidOperationException("Dock snapshot unavailable."),
            (_, _) => ValueTask.FromResult(
                StudioActionCompletion.Succeeded("Opened.")));
        var executor = new StudioActionExecutor(registry);

        var result = executor.EvaluateState(actionId, Context(7));

        Assert.Equal(StudioActionStateEvaluationStatus.Failed, result.Status);
        Assert.Contains(
            "Dock snapshot unavailable",
            result.FailureMessage,
            StringComparison.Ordinal);
    }

    [Fact]
    public async Task Execution_uses_the_exact_frozen_context_instance()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.scene.create-entity");
        StudioActionContextSnapshot? evaluatedContext = null;
        StudioActionContextSnapshot? handledContext = null;
        registry.Register(
            Definition(actionId),
            [Menu()],
            context =>
            {
                evaluatedContext = context;
                return StudioActionState.Available();
            },
            (context, _) =>
            {
                handledContext = context;
                return ValueTask.FromResult(
                    StudioActionCompletion.Succeeded("Created."));
            });
        var executor = new StudioActionExecutor(registry);
        var context = Context(7);

        var result = await executor.ExecuteAsync(actionId, context);

        Assert.Equal(StudioActionResultStatus.Succeeded, result.Status);
        Assert.Same(context, evaluatedContext);
        Assert.Same(context, handledContext);
    }

    private static void RegisterBlocked(
        StudioActionRegistry registry,
        StudioActionId actionId,
        StudioActionBlockKind blockKind,
        string reason,
        Action handlerCalled) =>
        registry.Register(
            Definition(actionId),
            [Menu()],
            _ => StudioActionState.Blocked(blockKind, reason),
            (_, _) =>
            {
                handlerCalled();
                return ValueTask.FromResult(
                    StudioActionCompletion.Succeeded("Executed."));
            });

    private static StudioActionDefinition Definition(StudioActionId actionId) =>
        new(actionId, actionId.Value, $"Execute {actionId}.", "Test");

    private static StudioActionPlacement Menu() =>
        new(
            new StudioActionPlacementId($"placement-{Guid.NewGuid():N}"),
            StudioActionPlacementKind.Menu,
            "Test/Action",
            "test",
            order: 0,
            StudioActionScope.Document);

    private static StudioActionPlacement ContextMenu() =>
        new(
            new StudioActionPlacementId($"placement-{Guid.NewGuid():N}"),
            StudioActionPlacementKind.ContextMenu,
            "Hierarchy/Entity",
            "test",
            order: 0,
            StudioActionScope.FocusedPanel);

    private static StudioActionContextSnapshot Context(
        ulong revision,
        StudioActionInvocationSource source = StudioActionInvocationSource.Menu,
        Guid? targetObjectId = null)
    {
        var sessionId = new ProjectSessionId(
            Guid.Parse("12345678-1234-1234-1234-123456789abc"));
        var sceneId = Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        var selection = targetObjectId is Guid objectId
            ? new StudioActionSelectionSnapshot([objectId], objectId)
            : StudioActionSelectionSnapshot.Empty;
        var target = targetObjectId is Guid contextObjectId
            ? StudioActionTarget.SceneObject(sessionId, sceneId, contextObjectId)
            : StudioActionTarget.Scene(sessionId, sceneId);
        return new StudioActionContextSnapshot(
            source,
            new StudioPresentationId("main-window"),
            new StudioPresentationId("hierarchy"),
            sessionId,
            sceneId,
            revision,
            selection,
            target,
            Guid.NewGuid(),
            Guid.NewGuid(),
            Guid.NewGuid());
    }
}

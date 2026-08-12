using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectDocumentTransitionCoordinatorTests
{
    [Fact]
    public async Task Clean_document_executes_transition_without_prompting()
    {
        var session = new ControlledProjectSession(CleanSnapshot());
        var prompt = new ControlledPrompt();
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.OpenProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    session.Current,
                    "Opened the replacement project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Completed, result.Status);
        Assert.True(result.MayProceed);
        Assert.Equal("Opened the replacement project.", result.Message);
        Assert.True(result.ProjectOperation?.Succeeded);
        Assert.Equal(1, transitionCalls);
        Assert.Empty(prompt.Requests);
        Assert.Equal(0, session.SaveCalls);
    }

    [Fact]
    public async Task Cancel_keeps_dirty_document_and_does_not_execute_transition()
    {
        var dirty = DirtySnapshot();
        var session = new ControlledProjectSession(dirty);
        var prompt = new ControlledPrompt(ProjectDocumentTransitionDecision.Cancel);
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    ProjectSessionSnapshot.NoProject,
                    "Closed the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Cancelled, result.Status);
        Assert.False(result.MayProceed);
        Assert.Same(dirty, session.Current);
        Assert.Equal(0, transitionCalls);
        Assert.Equal(0, session.SaveCalls);
        Assert.Equal(ProjectDocumentTransitionKind.CloseProject, prompt.Requests[0].Kind);
    }

    [Fact]
    public async Task Discard_allows_project_replacement_without_saving()
    {
        var session = new ControlledProjectSession(DirtySnapshot());
        var prompt = new ControlledPrompt(ProjectDocumentTransitionDecision.Discard);
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var replacement = CleanSnapshot(
            sessionId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
            sceneId: Guid.Parse("11111111-aaaa-bbbb-cccc-222222222222"));

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.OpenProject,
            (_, _) =>
            {
                session.SetCurrent(replacement);
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    replacement,
                    "Opened the replacement project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Completed, result.Status);
        Assert.Same(replacement, session.Current);
        Assert.Equal(0, session.SaveCalls);
        Assert.Equal(ProjectDocumentTransitionKind.OpenProject, prompt.Requests[0].Kind);
    }

    [Fact]
    public async Task Save_must_publish_clean_state_before_transition_executes()
    {
        var dirty = DirtySnapshot();
        var clean = CopyWithContentState(dirty, current: 2, saved: 2);
        var session = new ControlledProjectSession(dirty);
        session.SaveHandler = _ =>
        {
            session.SetCurrent(clean);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                clean,
                "Saved the active scene."));
        };
        var prompt = new ControlledPrompt(ProjectDocumentTransitionDecision.Save);
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) =>
            {
                transitionCalls++;
                session.SetCurrent(ProjectSessionSnapshot.NoProject);
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    ProjectSessionSnapshot.NoProject,
                    "Closed the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Completed, result.Status);
        Assert.Equal(1, session.SaveCalls);
        Assert.Equal(1, transitionCalls);
        Assert.False(session.Current.IsReady);
    }

    [Fact]
    public async Task Failed_save_preserves_dirty_document_and_blocks_transition()
    {
        var dirty = DirtySnapshot();
        var session = new ControlledProjectSession(dirty)
        {
            SaveHandler = _ => ValueTask.FromResult(ProjectSessionOperationResult.Failed(
                dirty,
                ProjectSessionFailureKind.IoFailure,
                "The scene file could not be written.")),
        };
        var prompt = new ControlledPrompt(ProjectDocumentTransitionDecision.Save);
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    ProjectSessionSnapshot.NoProject,
                    "Closed the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.SaveFailed, result.Status);
        Assert.Equal(ProjectSessionFailureKind.IoFailure, result.ProjectOperation?.FailureKind);
        Assert.Same(dirty, session.Current);
        Assert.Equal(0, transitionCalls);
    }

    [Fact]
    public async Task False_successful_save_that_remains_dirty_fails_closed()
    {
        var dirty = DirtySnapshot();
        var session = new ControlledProjectSession(dirty)
        {
            SaveHandler = _ => ValueTask.FromResult(ProjectSessionOperationResult.Success(
                dirty,
                "Reported success without a clean savepoint.")),
        };
        var coordinator = new ProjectDocumentTransitionCoordinator(
            session,
            new ControlledPrompt(ProjectDocumentTransitionDecision.Save));
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.OpenProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    session.Current,
                    "Opened the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.SaveFailed, result.Status);
        Assert.Equal(ProjectSessionFailureKind.InternalError, result.ProjectOperation?.FailureKind);
        Assert.Contains("clean authoritative", result.Message, StringComparison.Ordinal);
        Assert.Equal(0, transitionCalls);
    }

    [Fact]
    public async Task Scope_change_while_prompt_is_open_fails_stale_without_reprompting()
    {
        var first = DirtySnapshot();
        var second = DirtySnapshot(
            sessionId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
            sceneId: Guid.Parse("11111111-aaaa-bbbb-cccc-222222222222"));
        var session = new ControlledProjectSession(first);
        var prompt = new ControlledPrompt
        {
            Handler = (request, _) =>
            {
                session.SetCurrent(second);
                return ValueTask.FromResult(ProjectDocumentTransitionDecision.Discard);
            },
        };
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.OpenProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    session.Current,
                    "Opened the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Stale, result.Status);
        Assert.Single(prompt.Requests);
        Assert.Equal(0, transitionCalls);
    }

    [Fact]
    public async Task Clean_scope_change_while_prompt_is_open_cannot_close_the_new_document()
    {
        var first = DirtySnapshot();
        var second = CopyWithContentState(
            DirtySnapshot(
                sessionId: Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
                sceneId: Guid.Parse("11111111-aaaa-bbbb-cccc-222222222222")),
            current: 4,
            saved: 4);
        var session = new ControlledProjectSession(first);
        var prompt = new ControlledPrompt
        {
            Handler = (_, _) =>
            {
                session.SetCurrent(second);
                return ValueTask.FromResult(ProjectDocumentTransitionDecision.Discard);
            },
        };
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        var transitionCalls = 0;

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) =>
            {
                transitionCalls++;
                return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                    ProjectSessionSnapshot.NoProject,
                    "Closed the project."));
            });

        Assert.Equal(ProjectDocumentTransitionStatus.Stale, result.Status);
        Assert.Same(second, session.Current);
        Assert.Equal(0, transitionCalls);
    }

    [Fact]
    public async Task New_edit_after_save_is_prompted_before_transition_continues()
    {
        var first = DirtySnapshot();
        var saved = CopyWithContentState(first, current: 2, saved: 2);
        var editedAgain = CopyWithContentState(first, current: 3, saved: 2, revision: 3);
        var session = new ControlledProjectSession(first);
        session.SaveHandler = _ =>
        {
            session.SetCurrent(saved);
            session.SetCurrent(editedAgain);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                saved,
                "Saved the active scene."));
        };
        var prompt = new ControlledPrompt(
            ProjectDocumentTransitionDecision.Save,
            ProjectDocumentTransitionDecision.Cancel);
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) => throw new InvalidOperationException(
                "The stale decision must not close."));

        Assert.Equal(ProjectDocumentTransitionStatus.Cancelled, result.Status);
        Assert.Same(editedAgain, session.Current);
        Assert.Equal(2, prompt.Requests.Count);
    }

    [Fact]
    public async Task Exit_preparation_resolves_dirty_state_without_closing_project()
    {
        var dirty = DirtySnapshot();
        var session = new ControlledProjectSession(dirty);
        var coordinator = new ProjectDocumentTransitionCoordinator(
            session,
            new ControlledPrompt(ProjectDocumentTransitionDecision.Discard));

        var result = await coordinator.PrepareExitAsync();

        Assert.Equal(ProjectDocumentTransitionStatus.Completed, result.Status);
        Assert.Same(dirty, session.Current);
        Assert.Equal(0, session.CloseCalls);
    }

    [Fact]
    public async Task Concurrent_transition_fails_fast_as_busy()
    {
        var dirty = DirtySnapshot();
        var session = new ControlledProjectSession(dirty);
        var promptEntered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releasePrompt = new TaskCompletionSource<ProjectDocumentTransitionDecision>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var prompt = new ControlledPrompt
        {
            Handler = (_, _) =>
            {
                promptEntered.TrySetResult();
                return new ValueTask<ProjectDocumentTransitionDecision>(releasePrompt.Task);
            },
        };
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);

        var first = coordinator.PrepareExitAsync().AsTask();
        await promptEntered.Task;
        Assert.True(coordinator.IsTransitionActive);

        var second = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CloseProject,
            (_, _) => throw new InvalidOperationException(
                "A busy transition must not execute."));

        Assert.Equal(ProjectDocumentTransitionStatus.Busy, second.Status);
        releasePrompt.SetResult(ProjectDocumentTransitionDecision.Cancel);
        Assert.Equal(ProjectDocumentTransitionStatus.Cancelled, (await first).Status);
        Assert.False(coordinator.IsTransitionActive);
    }

    [Fact]
    public async Task Cancellation_releases_transition_for_a_later_request()
    {
        var session = new ControlledProjectSession(DirtySnapshot());
        var prompt = new ControlledPrompt
        {
            Handler = async (_, token) =>
            {
                await Task.Delay(Timeout.InfiniteTimeSpan, token);
                return ProjectDocumentTransitionDecision.Discard;
            },
        };
        var coordinator = new ProjectDocumentTransitionCoordinator(session, prompt);
        using var cancellation = new CancellationTokenSource();

        var cancelled = coordinator.PrepareExitAsync(cancellation.Token).AsTask();
        cancellation.Cancel();

        Assert.Equal(ProjectDocumentTransitionStatus.Cancelled, (await cancelled).Status);
        session.SetCurrent(CleanSnapshot());
        Assert.Equal(
            ProjectDocumentTransitionStatus.Completed,
            (await coordinator.PrepareExitAsync()).Status);
    }

    [Fact]
    public async Task Transition_exception_is_typed_and_does_not_leave_coordinator_busy()
    {
        var session = new ControlledProjectSession(CleanSnapshot());
        var coordinator = new ProjectDocumentTransitionCoordinator(
            session,
            new ControlledPrompt());

        var result = await coordinator.ExecuteAsync(
            ProjectDocumentTransitionKind.CreateProject,
            (_, _) => throw new InvalidOperationException("Create adapter failed."));

        Assert.Equal(ProjectDocumentTransitionStatus.TransitionFailed, result.Status);
        Assert.Equal(ProjectSessionFailureKind.InternalError, result.ProjectOperation?.FailureKind);
        Assert.Equal("The document transition failed unexpectedly.", result.Message);
        Assert.False(coordinator.IsTransitionActive);
    }

    private static ProjectSessionSnapshot DirtySnapshot(
        Guid? sessionId = null,
        Guid? sceneId = null) =>
        Snapshot(sessionId, sceneId, current: 2, saved: 1, revision: 2);

    private static ProjectSessionSnapshot CleanSnapshot(
        Guid? sessionId = null,
        Guid? sceneId = null) =>
        Snapshot(sessionId, sceneId, current: 2, saved: 2, revision: 2);

    private static ProjectSessionSnapshot Snapshot(
        Guid? sessionId,
        Guid? sceneId,
        ulong current,
        ulong saved,
        ulong revision)
    {
        var project = new ActiveProjectSnapshot(
            new ProjectSessionId(sessionId ?? Guid.Parse(
                "12345678-1234-1234-1234-123456789abc")),
            Guid.Parse("87654321-4321-4321-4321-cba987654321"),
            "Sample",
            "C:\\Projects\\Sample");
        var document = new SceneDocumentSnapshot(
            sceneId ?? Guid.Parse("11111111-2222-3333-4444-555555555555"),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision,
            Math.Min(revision, saved),
            []);
        return ProjectSessionSnapshot.Ready(
            project,
            document,
            new ContentStateId(current),
            new ContentStateId(saved),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
    }

    private static ProjectSessionSnapshot CopyWithContentState(
        ProjectSessionSnapshot source,
        ulong current,
        ulong saved,
        ulong? revision = null)
    {
        var document = source.Document!;
        return ProjectSessionSnapshot.Ready(
            source.Project!,
            new SceneDocumentSnapshot(
                document.SceneId,
                document.Path,
                revision ?? document.Revision,
                Math.Min(revision ?? document.Revision, saved),
                document.Entities),
            new ContentStateId(current),
            new ContentStateId(saved),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
    }

    private sealed class ControlledPrompt(
        params ProjectDocumentTransitionDecision[] decisions) :
        IProjectDocumentTransitionPrompt
    {
        private readonly Queue<ProjectDocumentTransitionDecision> decisions_ =
            new(decisions);

        public List<ProjectDocumentTransitionPrompt> Requests { get; } = [];

        public Func<ProjectDocumentTransitionPrompt, CancellationToken,
            ValueTask<ProjectDocumentTransitionDecision>>? Handler
        { get; set; }

        public ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
            ProjectDocumentTransitionPrompt prompt,
            CancellationToken cancellationToken = default)
        {
            Requests.Add(prompt);
            cancellationToken.ThrowIfCancellationRequested();
            return Handler?.Invoke(prompt, cancellationToken)
                ?? ValueTask.FromResult(decisions_.Dequeue());
        }
    }

    private sealed class ControlledProjectSession(
        ProjectSessionSnapshot current) : IProjectSession
    {
        public event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;

        public ProjectSessionSnapshot Current { get; private set; } = current;

        public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
            SaveHandler
        { get; set; }

        public int SaveCalls { get; private set; }

        public int CloseCalls { get; private set; }

        public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
            string parentDirectory,
            string projectName,
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
            string projectPath,
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default)
        {
            CloseCalls++;
            throw new NotSupportedException();
        }

        public ValueTask<ProjectSessionOperationResult> PrepareExitAsync(
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(ProjectSessionOperationResult.Success(
                Current,
                "Prepared test exit."));

        public ValueTask<ProjectSessionOperationResult> CreateEntityAsync(
            string name,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(
            string name,
            SceneMeshReference mesh,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(
            Guid objectId,
            string name,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(
            Guid objectId,
            TransformValue transform,
            ProjectSessionEditContext context,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> UndoAsync(
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> RedoAsync(
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
            CancellationToken cancellationToken = default)
        {
            SaveCalls++;
            return SaveHandler?.Invoke(cancellationToken)
                ?? throw new InvalidOperationException("No save result was configured.");
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetCurrent(ProjectSessionSnapshot snapshot)
        {
            Current = snapshot;
            SnapshotChanged?.Invoke(
                this,
                new ProjectSessionSnapshotChangedEventArgs(
                    snapshot,
                    originatingEditId: null));
        }
    }
}

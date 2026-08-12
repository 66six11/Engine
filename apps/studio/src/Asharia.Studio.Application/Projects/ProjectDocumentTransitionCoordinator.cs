using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Projects;

public sealed class ProjectDocumentTransitionCoordinator
{
    private readonly IProjectSession projectSession_;
    private readonly IProjectDocumentTransitionPrompt prompt_;
    private int transitionActive_;

    public ProjectDocumentTransitionCoordinator(
        IProjectSession projectSession,
        IProjectDocumentTransitionPrompt prompt)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(prompt);
        projectSession_ = projectSession;
        prompt_ = prompt;
    }

    public bool IsTransitionActive => Volatile.Read(ref transitionActive_) != 0;

    public ValueTask<ProjectDocumentTransitionResult> ExecuteAsync(
        ProjectDocumentTransitionKind kind,
        Func<ProjectDocumentTransitionExpectation, CancellationToken,
            ValueTask<ProjectSessionOperationResult>> transition,
        CancellationToken cancellationToken = default)
    {
        if (kind == ProjectDocumentTransitionKind.ExitStudio || !Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        ArgumentNullException.ThrowIfNull(transition);
        return RunExclusiveAsync(kind, transition, cancellationToken);
    }

    public ValueTask<ProjectDocumentTransitionResult> PrepareExitAsync(
        CancellationToken cancellationToken = default) =>
        RunExclusiveAsync(
            ProjectDocumentTransitionKind.ExitStudio,
            transition: null,
            cancellationToken);

    private async ValueTask<ProjectDocumentTransitionResult> RunExclusiveAsync(
        ProjectDocumentTransitionKind kind,
        Func<ProjectDocumentTransitionExpectation, CancellationToken,
            ValueTask<ProjectSessionOperationResult>>? transition,
        CancellationToken cancellationToken)
    {
        if (Interlocked.CompareExchange(ref transitionActive_, 1, 0) != 0)
        {
            return ProjectDocumentTransitionResult.Busy();
        }

        try
        {
            var preparation = await PrepareTransitionAsync(kind, cancellationToken);
            if (preparation.Result is not null)
            {
                return preparation.Result;
            }

            var operation = transition is null
                ? await projectSession_.PrepareExitAsync(
                    preparation.Expectation!, cancellationToken)
                : await transition(preparation.Expectation!, cancellationToken);
            ArgumentNullException.ThrowIfNull(operation);
            if (operation.FailureKind ==
                ProjectSessionFailureKind.StaleDocumentTransition)
            {
                return ProjectDocumentTransitionResult.Stale(operation);
            }
            return operation.Succeeded
                ? ProjectDocumentTransitionResult.Completed(operation)
                : ProjectDocumentTransitionResult.TransitionFailed(operation);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return ProjectDocumentTransitionResult.Cancelled();
        }
        catch (Exception exception)
        {
            _ = exception;
            return ProjectDocumentTransitionResult.TransitionFailed(
                ProjectSessionOperationResult.Failed(
                    projectSession_.Current,
                    ProjectSessionFailureKind.InternalError,
                    "The document transition failed unexpectedly."));
        }
        finally
        {
            Volatile.Write(ref transitionActive_, 0);
        }
    }

    private async ValueTask<TransitionPreparation> PrepareTransitionAsync(
        ProjectDocumentTransitionKind kind,
        CancellationToken cancellationToken)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var beforePrompt = projectSession_.Current;
            if (!beforePrompt.IsDirty)
            {
                return TransitionPreparation.Prepared(
                    ProjectDocumentTransitionExpectation.Capture(beforePrompt));
            }

            var prompt = ProjectDocumentTransitionPrompt.FromDirtySnapshot(
                kind,
                beforePrompt);
            var decision = await prompt_.ChooseAsync(prompt, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            if (!Enum.IsDefined(decision))
            {
                throw new InvalidOperationException(
                    "The document transition prompt returned an unknown decision.");
            }
            if (decision == ProjectDocumentTransitionDecision.Cancel)
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.Cancelled());
            }

            var afterPrompt = projectSession_.Current;
            if (!SameDocument(prompt, afterPrompt))
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.Stale());
            }
            if (!afterPrompt.IsDirty)
            {
                return TransitionPreparation.Prepared(
                    ProjectDocumentTransitionExpectation.Capture(afterPrompt));
            }
            if (!StillMatchesPrompt(prompt, afterPrompt))
            {
                continue;
            }
            if (decision == ProjectDocumentTransitionDecision.Discard)
            {
                return TransitionPreparation.Prepared(
                    ProjectDocumentTransitionExpectation.CaptureAfterDiscard(afterPrompt));
            }

            var save = await projectSession_.SaveSceneAsync(cancellationToken);
            ArgumentNullException.ThrowIfNull(save);
            if (!save.Succeeded)
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.SaveFailed(save));
            }
            if (!IsCleanSaveOfPromptedDocument(prompt, save.Current))
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.SaveFailed(
                        InvalidSaveResult(save.Current)));
            }

            var afterSave = projectSession_.Current;
            if (!SameDocument(prompt, afterSave))
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.Stale());
            }
            if (SameContentState(save.Current, afterSave))
            {
                return afterSave.IsDirty
                    ? TransitionPreparation.Failed(
                        ProjectDocumentTransitionResult.SaveFailed(
                            InvalidSaveResult(afterSave)))
                    : TransitionPreparation.Prepared(
                        ProjectDocumentTransitionExpectation.Capture(afterSave));
            }
            if (SameContentState(beforePrompt, afterSave))
            {
                return TransitionPreparation.Failed(
                    ProjectDocumentTransitionResult.SaveFailed(
                        InvalidSaveResult(afterSave)));
            }

            // A new edit completed after the save. Resolve that newer dirty state
            // instead of applying a stale discard/close decision to it.
        }
    }

    private static bool StillMatchesPrompt(
        ProjectDocumentTransitionPrompt prompt,
        ProjectSessionSnapshot snapshot) =>
        SameDocument(prompt, snapshot) &&
        snapshot.IsDirty &&
        snapshot.Document!.Revision == prompt.DocumentRevision &&
        snapshot.CurrentContentStateId == prompt.CurrentContentStateId &&
        snapshot.SavedContentStateId == prompt.SavedContentStateId;

    private static bool IsCleanSaveOfPromptedDocument(
        ProjectDocumentTransitionPrompt prompt,
        ProjectSessionSnapshot snapshot) =>
        SameDocument(prompt, snapshot) && !snapshot.IsDirty;

    private static bool SameDocument(
        ProjectDocumentTransitionPrompt prompt,
        ProjectSessionSnapshot snapshot) =>
        snapshot.Project?.SessionId == prompt.SessionId &&
        snapshot.Project?.ProjectId == prompt.ProjectId &&
        snapshot.Document?.SceneId == prompt.SceneId;

    private static bool SameContentState(
        ProjectSessionSnapshot left,
        ProjectSessionSnapshot right) =>
        left.CurrentContentStateId == right.CurrentContentStateId &&
        left.SavedContentStateId == right.SavedContentStateId;

    private static ProjectSessionOperationResult InvalidSaveResult(
        ProjectSessionSnapshot snapshot) =>
        ProjectSessionOperationResult.Failed(
            snapshot,
            ProjectSessionFailureKind.InternalError,
            "The scene save completed without publishing a clean authoritative document state.");

    private sealed record TransitionPreparation(
        ProjectDocumentTransitionExpectation? Expectation,
        ProjectDocumentTransitionResult? Result)
    {
        public static TransitionPreparation Prepared(
            ProjectDocumentTransitionExpectation expectation) =>
            new(expectation, Result: null);

        public static TransitionPreparation Failed(
            ProjectDocumentTransitionResult result) =>
            new(Expectation: null, result);
    }
}

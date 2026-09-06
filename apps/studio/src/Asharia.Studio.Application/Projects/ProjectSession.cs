using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;

namespace Asharia.Studio.Application.Projects;

public sealed class ProjectSession : IProjectSession
{
    private readonly IProjectDescriptorGateway projectGateway_;
    private readonly ISceneDocumentGateway sceneGateway_;
    private readonly SemaphoreSlim operationGate_ = new(1, 1);
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly object snapshotGate_ = new();
    private readonly SceneEditHistory editHistory_ = new();
    private ProjectSessionSnapshot current_ = ProjectSessionSnapshot.NoProject;
    private ISceneDocumentConnection? activeDocument_;
    private ulong nextContentStateValue_;
    private int disposeStarted_;
    private bool exitPrepared_;

    public ProjectSession(
        IProjectDescriptorGateway projectGateway,
        ISceneDocumentGateway sceneGateway)
    {
        ArgumentNullException.ThrowIfNull(projectGateway);
        ArgumentNullException.ThrowIfNull(sceneGateway);
        projectGateway_ = projectGateway;
        sceneGateway_ = sceneGateway;
    }

    public event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;

    public ProjectSessionSnapshot Current
    {
        get
        {
            lock (snapshotGate_)
            {
                return current_;
            }
        }
    }

    public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
        string parentDirectory,
        string projectName,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(parentDirectory);
        ArgumentException.ThrowIfNullOrWhiteSpace(projectName);
        ArgumentNullException.ThrowIfNull(expectation);
        return OpenProjectCoreAsync(
            token => projectGateway_.CreateMinimalProjectAsync(
                parentDirectory,
                projectName,
                Guid.NewGuid(),
                token),
            descriptor => $"Created project '{descriptor.ProjectName}' and opened its default scene.",
            expectation,
            cancellationToken);
    }

    public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectPath);
        ArgumentNullException.ThrowIfNull(expectation);
        return OpenProjectCoreAsync(
            token => projectGateway_.OpenProjectAsync(projectPath, token),
            descriptor => $"Opened project '{descriptor.ProjectName}' and its default scene.",
            expectation,
            cancellationToken);
    }

    public async ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(expectation);
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            var transitionFailure = ValidateDocumentTransition(expectation);
            if (transitionFailure is not null)
            {
                return transitionFailure;
            }
            var document = activeDocument_;
            if (document is null)
            {
                return ProjectSessionOperationResult.Success(
                    Current,
                    "No project is open.");
            }

            await document.DisposeAsync().ConfigureAwait(false);
            activeDocument_ = null;
            editHistory_.Reset();
            Publish(ProjectSessionSnapshot.NoProject);
            return ProjectSessionOperationResult.Success(
                ProjectSessionSnapshot.NoProject,
                "Closed the active scene document and project.");
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return ProjectSessionOperationResult.Failed(
                Current,
                ProjectSessionFailureKind.InternalError,
                DiagnosticMessage(exception, "The active scene document could not be closed."));
        }
        finally
        {
            operationGate_.Release();
        }
    }

    public async ValueTask<ProjectSessionOperationResult> PrepareExitAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(expectation);
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            var transitionFailure = ValidateDocumentTransition(expectation);
            if (transitionFailure is not null)
            {
                return transitionFailure;
            }

            exitPrepared_ = true;
            return ProjectSessionOperationResult.Success(
                Current,
                "Prepared the active project session for Studio shutdown.");
        }
        finally
        {
            operationGate_.Release();
        }
    }

    public ValueTask<ProjectSessionOperationResult> CreateEntityAsync(
        string name,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(name);
        var objectId = Guid.NewGuid();
        return EditSceneAsync(
            (document, snapshot) => document.CreateEntityAsync(
                objectId,
                name,
                snapshot.Revision,
                CancellationToken.None),
            "Created a scene entity.",
            cancellationToken,
            objectId,
            expectedMesh: null,
            operationKind: SceneOperationKind.NonUndoableMutation);
    }

    public ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(
        string name,
        SceneMeshReference mesh,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(name);
        if (mesh.AssetId == Guid.Empty)
        {
            throw new ArgumentException("Mesh asset id must not be empty.", nameof(mesh));
        }
        var objectId = Guid.NewGuid();
        return EditSceneAsync(
            (document, snapshot) => document.CreateMeshEntityAsync(
                objectId,
                name,
                mesh,
                snapshot.Revision,
                CancellationToken.None),
            "Created a mesh scene entity.",
            cancellationToken,
            objectId,
            mesh,
            operationKind: SceneOperationKind.NonUndoableMutation);
    }

    public ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(
        Guid objectId,
        string name,
        CancellationToken cancellationToken = default)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        ArgumentNullException.ThrowIfNull(name);
        return EditSceneAsync(
            (document, snapshot) => document.SetEntityNameAsync(
                objectId,
                name,
                snapshot.Revision,
                CancellationToken.None),
            "Updated the scene entity name.",
            cancellationToken,
            operationKind: SceneOperationKind.NonUndoableMutation);
    }

    public ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(
        Guid objectId,
        TransformValue transform,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken = default)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        if (!context.EditId.IsValid)
        {
            throw new ArgumentException(
                "Project edit id must be valid.",
                nameof(context));
        }
        return SetEntityTransformCoreAsync(
            objectId,
            transform,
            context,
            cancellationToken);
    }

    public ValueTask<ProjectSessionOperationResult> SetEntityMeshAsync(
        Guid objectId,
        SceneMeshReference? mesh,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken = default)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        if (mesh?.AssetId == Guid.Empty)
        {
            throw new ArgumentException("Mesh asset id must not be empty.", nameof(mesh));
        }
        if (!context.EditId.IsValid)
        {
            throw new ArgumentException(
                "Project edit id must be valid.",
                nameof(context));
        }
        return SetEntityMeshCoreAsync(
            objectId,
            mesh,
            context,
            cancellationToken);
    }

    public ValueTask<ProjectSessionOperationResult> UndoAsync(
        CancellationToken cancellationToken = default) =>
        ReplaySceneEditAsync(isUndo: true, cancellationToken);

    public ValueTask<ProjectSessionOperationResult> RedoAsync(
        CancellationToken cancellationToken = default) =>
        ReplaySceneEditAsync(isUndo: false, cancellationToken);

    public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default) =>
        EditSceneAsync(
            (document, snapshot) => document.SaveAsync(
                snapshot.Revision,
                CancellationToken.None),
            "Saved the active scene.",
            cancellationToken,
            operationKind: SceneOperationKind.Save);

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposeStarted_, 1) != 0)
        {
            return;
        }

        await lifetimeCancellation_.CancelAsync().ConfigureAwait(false);
        await operationGate_.WaitAsync().ConfigureAwait(false);
        try
        {
            var document = activeDocument_;
            activeDocument_ = null;
            editHistory_.Reset();
            lock (snapshotGate_)
            {
                current_ = ProjectSessionSnapshot.NoProject;
            }
            if (document is not null)
            {
                await document.DisposeAsync().ConfigureAwait(false);
            }
        }
        finally
        {
            operationGate_.Release();
            operationGate_.Dispose();
            lifetimeCancellation_.Dispose();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> OpenProjectCoreAsync(
        Func<CancellationToken, ValueTask<ProjectDescriptorOperationResult>> projectOperation,
        Func<ProjectDescriptorSnapshot, string> successMessage,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            var transitionFailure = ValidateDocumentTransition(expectation);
            if (transitionFailure is not null)
            {
                return transitionFailure;
            }
            ProjectDescriptorOperationResult projectResult;
            try
            {
                projectResult = await projectOperation(linkedCancellation.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception)
            {
                return ProjectSessionOperationResult.Failed(
                    Current,
                    ProjectSessionFailureKind.InternalError,
                    DiagnosticMessage(exception, "The project adapter failed without a diagnostic."));
            }

            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (!projectResult.Succeeded)
            {
                var failure = projectResult.Failure!;
                return ProjectSessionOperationResult.Failed(
                    Current,
                    MapProjectFailure(failure.Kind),
                    failure.Message);
            }

            var descriptor = projectResult.Project!;
            SceneDocumentOpenResult sceneResult;
            try
            {
                sceneResult = await sceneGateway_.OpenDefaultAsync(
                    descriptor.RootPath,
                    Guid.NewGuid(),
                    linkedCancellation.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception)
            {
                return ProjectSessionOperationResult.Failed(
                    Current,
                    ProjectSessionFailureKind.InternalError,
                    DiagnosticMessage(exception, "The scene document adapter failed without a diagnostic."));
            }

            if (!sceneResult.Succeeded)
            {
                var failure = sceneResult.Failure!;
                return ProjectSessionOperationResult.Failed(
                    Current,
                    MapSceneFailure(failure.Kind),
                    failure.Message);
            }

            var nextDocument = sceneResult.Connection!;
            if (linkedCancellation.IsCancellationRequested)
            {
                await nextDocument.DisposeAsync().ConfigureAwait(false);
                linkedCancellation.Token.ThrowIfCancellationRequested();
            }

            var previousDocument = activeDocument_;
            if (previousDocument is not null)
            {
                try
                {
                    await previousDocument.DisposeAsync().ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    await nextDocument.DisposeAsync().ConfigureAwait(false);
                    activeDocument_ = null;
                    editHistory_.Reset();
                    Publish(ProjectSessionSnapshot.NoProject);
                    return ProjectSessionOperationResult.Failed(
                        ProjectSessionSnapshot.NoProject,
                        ProjectSessionFailureKind.InternalError,
                        DiagnosticMessage(
                            exception,
                            "The previous scene document could not be closed during project replacement."));
                }
            }

            activeDocument_ = nextDocument;
            editHistory_.Reset();
            var initialContentStateId = AllocateContentStateId();
            var next = ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    ProjectSessionId.CreateNew(),
                    descriptor.ProjectId,
                    descriptor.ProjectName,
                    descriptor.RootPath),
                sceneResult.Document!,
                initialContentStateId,
                initialContentStateId,
                canUndo: false,
                canRedo: false,
                undoLabel: null,
                redoLabel: null);
            Publish(next);
            return ProjectSessionOperationResult.Success(next, successMessage(descriptor));
        }
        finally
        {
            operationGate_.Release();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> EditSceneAsync(
        Func<ISceneDocumentConnection, SceneDocumentSnapshot,
            ValueTask<SceneDocumentOperationResult>> operation,
        string successMessage,
        CancellationToken cancellationToken,
        Guid? createdObjectId = null,
        SceneMeshReference? expectedMesh = null,
        ProjectEditId? originatingEditId = null,
        SceneOperationKind operationKind = SceneOperationKind.NonUndoableMutation)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (exitPrepared_)
            {
                return ExitPreparedFailure(originatingEditId);
            }
            var before = Current;
            var document = activeDocument_;
            if (document is null || before.Project is null || before.Document is null)
            {
                return ProjectSessionOperationResult.Failed(
                    before,
                    ProjectSessionFailureKind.NoProject,
                    "No editable scene document is open.",
                    originatingEditId);
            }

            SceneDocumentOperationResult result;
            try
            {
                // Once an edit enters the native owner lane, it runs to an authoritative result.
                result = await operation(document, before.Document).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                return await RecoverFromUncertainOperationAsync(
                    document,
                    before,
                    originatingEditId,
                    contentMayHaveChanged: operationKind != SceneOperationKind.Save,
                    DiagnosticMessage(exception, "The scene edit failed without a diagnostic."))
                    .ConfigureAwait(false);
            }

            if (!IsSameDocument(before.Document, result.Current) ||
                result.Current.Revision < before.Document.Revision)
            {
                return await RecoverFromUncertainOperationAsync(
                    document,
                    before,
                    originatingEditId,
                    contentMayHaveChanged: operationKind != SceneOperationKind.Save,
                    "The scene operation returned an inconsistent authoritative snapshot.")
                    .ConfigureAwait(false);
            }

            if (!result.Succeeded)
            {
                if (result.Failure!.Kind == SceneDocumentFailureKind.AuthoritativeStateUnknown)
                {
                    return await RecoverFromUncertainOperationAsync(
                        document,
                        before,
                        originatingEditId,
                        contentMayHaveChanged: operationKind != SceneOperationKind.Save,
                        result.Failure.Message).ConfigureAwait(false);
                }
                var failedSnapshot = result.Current.Revision == before.Document.Revision
                    ? SnapshotWithDocument(before, result.Current)
                    : ResetHistoryAfterUncertainMutation(before, result.Current);
                Publish(
                    failedSnapshot,
                    originatingEditId,
                    originatingEditId is null ? null : false);
                var failure = result.Failure!;
                return ProjectSessionOperationResult.Failed(
                    failedSnapshot,
                    MapSceneFailure(failure.Kind),
                    failure.Message,
                    originatingEditId);
            }
            if (createdObjectId is Guid createdId &&
                !ContainsCreatedObject(result.Current, createdId, expectedMesh))
            {
                var uncertainSnapshot = ResetHistoryAfterUncertainMutation(before, result.Current);
                Publish(
                    uncertainSnapshot,
                    originatingEditId,
                    originatingEditId is null ? null : false);
                return ProjectSessionOperationResult.Failed(
                    uncertainSnapshot,
                    ProjectSessionFailureKind.InternalError,
                    "The successful scene create receipt is absent from the " +
                    "authoritative snapshot.",
                    originatingEditId);
            }

            var next = operationKind switch
            {
                SceneOperationKind.Save => SnapshotWithState(
                    before,
                    result.Current,
                    before.CurrentContentStateId,
                    before.CurrentContentStateId),
                SceneOperationKind.NonUndoableMutation
                    when result.Current.Revision != before.Document.Revision =>
                    ResetHistoryAfterChangedNonUndoableMutation(before, result.Current),
                _ => SnapshotWithDocument(before, result.Current),
            };
            Publish(
                next,
                originatingEditId,
                originatingEditId is null ? null : true);
            return ProjectSessionOperationResult.Success(
                next,
                successMessage,
                createdObjectId,
                originatingEditId);
        }
        finally
        {
            operationGate_.Release();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> SetEntityTransformCoreAsync(
        Guid objectId,
        TransformValue transform,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (exitPrepared_)
            {
                return ExitPreparedFailure(context.EditId);
            }
            var before = Current;
            var document = activeDocument_;
            if (document is null || before.Project is null || before.Document is null)
            {
                return ProjectSessionOperationResult.Failed(
                    before,
                    ProjectSessionFailureKind.NoProject,
                    "No editable scene document is open.",
                    context.EditId);
            }

            var beforeEntity = FindEntity(before.Document, objectId);
            SceneDocumentOperationResult result;
            try
            {
                result = await document.SetEntityTransformAsync(
                    objectId,
                    transform,
                    context.ExpectedRevision,
                    CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                return await RecoverFromUncertainOperationAsync(
                    document,
                    before,
                    context.EditId,
                    contentMayHaveChanged: true,
                    DiagnosticMessage(
                        exception,
                        "The scene Transform edit failed without a diagnostic."))
                    .ConfigureAwait(false);
            }

            if (!result.Succeeded)
            {
                return await FinishTypedSceneEditFailureAsync(
                    document,
                    before,
                    result,
                    context.EditId).ConfigureAwait(false);
            }

            if (!TryValidateTransformReceipt(
                    before.Document,
                    beforeEntity,
                    objectId,
                    transform,
                    context.ExpectedRevision,
                    result,
                    requireChanged: false,
                    out var receipt))
            {
                return await FinishUncertainSceneEditResultAsync(
                    document,
                    before,
                    result.Current,
                    context.EditId,
                    "The successful scene Transform receipt did not match the request and authoritative snapshot.")
                    .ConfigureAwait(false);
            }

            if (!receipt.Changed)
            {
                var unchanged = SnapshotWithDocument(before, result.Current);
                Publish(unchanged, context.EditId, originatingEditSucceeded: true);
                return ProjectSessionOperationResult.Success(
                    unchanged,
                    "The scene entity Transform was already current.",
                    originatingEditId: context.EditId);
            }

            var afterContentStateId = AllocateContentStateId();
            editHistory_.Commit(new SceneTransformHistoryEntry(
                before.Document.SceneId,
                objectId,
                TransformLabel(beforeEntity),
                context.EditId,
                receipt.BeforeTransform,
                receipt.AfterTransform,
                before.CurrentContentStateId,
                afterContentStateId,
                SceneEditHistory.TransformEntryEstimatedBytes));
            var next = SnapshotWithState(
                before,
                result.Current,
                afterContentStateId,
                before.SavedContentStateId);
            Publish(next, context.EditId, originatingEditSucceeded: true);
            return ProjectSessionOperationResult.Success(
                next,
                "Updated the scene entity Transform.",
                originatingEditId: context.EditId);
        }
        finally
        {
            operationGate_.Release();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> SetEntityMeshCoreAsync(
        Guid objectId,
        SceneMeshReference? mesh,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (exitPrepared_)
            {
                return ExitPreparedFailure(context.EditId);
            }
            var before = Current;
            var document = activeDocument_;
            if (document is null || before.Project is null || before.Document is null)
            {
                return ProjectSessionOperationResult.Failed(
                    before,
                    ProjectSessionFailureKind.NoProject,
                    "No editable scene document is open.",
                    context.EditId);
            }

            var beforeEntity = FindEntity(before.Document, objectId);
            SceneDocumentOperationResult result;
            try
            {
                result = await document.SetEntityMeshAsync(
                    objectId,
                    mesh,
                    context.ExpectedRevision,
                    CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                return await RecoverFromUncertainOperationAsync(
                    document,
                    before,
                    context.EditId,
                    contentMayHaveChanged: true,
                    DiagnosticMessage(
                        exception,
                        "The scene Mesh edit failed without a diagnostic."))
                    .ConfigureAwait(false);
            }

            if (!result.Succeeded)
            {
                return await FinishTypedSceneEditFailureAsync(
                    document,
                    before,
                    result,
                    context.EditId).ConfigureAwait(false);
            }

            if (!TryValidateMeshReceipt(
                    before.Document,
                    beforeEntity,
                    objectId,
                    mesh,
                    context.ExpectedRevision,
                    result,
                    requireChanged: false,
                    out var receipt))
            {
                return await FinishUncertainSceneEditResultAsync(
                    document,
                    before,
                    result.Current,
                    context.EditId,
                    "The successful scene Mesh receipt did not match the request and authoritative snapshot.")
                    .ConfigureAwait(false);
            }

            if (!receipt.Changed)
            {
                var unchanged = SnapshotWithDocument(before, result.Current);
                Publish(unchanged, context.EditId, originatingEditSucceeded: true);
                return ProjectSessionOperationResult.Success(
                    unchanged,
                    "The scene entity Mesh was already current.",
                    originatingEditId: context.EditId);
            }

            var afterContentStateId = AllocateContentStateId();
            editHistory_.Commit(new SceneMeshHistoryEntry(
                before.Document.SceneId,
                objectId,
                MeshLabel(beforeEntity),
                context.EditId,
                receipt.BeforeMesh,
                receipt.AfterMesh,
                before.CurrentContentStateId,
                afterContentStateId,
                SceneEditHistory.MeshEntryEstimatedBytes));
            var next = SnapshotWithState(
                before,
                result.Current,
                afterContentStateId,
                before.SavedContentStateId);
            Publish(next, context.EditId, originatingEditSucceeded: true);
            return ProjectSessionOperationResult.Success(
                next,
                "Updated the scene entity Mesh.",
                originatingEditId: context.EditId);
        }
        finally
        {
            operationGate_.Release();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> ReplaySceneEditAsync(
        bool isUndo,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (exitPrepared_)
            {
                return ExitPreparedFailure();
            }
            var before = Current;
            var document = activeDocument_;
            if (document is null || before.Project is null || before.Document is null)
            {
                return ProjectSessionOperationResult.Failed(
                    before,
                    ProjectSessionFailureKind.NoProject,
                    "No editable scene document is open.");
            }

            var entry = isUndo ? editHistory_.UndoCandidate : editHistory_.RedoCandidate;
            if (entry is null)
            {
                return ProjectSessionOperationResult.Failed(
                    before,
                    ProjectSessionFailureKind.InvalidInput,
                    isUndo ? "There is no scene edit to Undo." : "There is no scene edit to Redo.");
            }

            var operationEditId = ProjectEditId.CreateNew();
            var transformEntry = entry as SceneTransformHistoryEntry;
            var meshEntry = entry as SceneMeshHistoryEntry;
            var expectedTransform = isUndo ? transformEntry?.AfterTransform : transformEntry?.BeforeTransform;
            var targetTransform = isUndo ? transformEntry?.BeforeTransform : transformEntry?.AfterTransform;
            var expectedMesh = isUndo ? meshEntry?.AfterMesh : meshEntry?.BeforeMesh;
            var targetMesh = isUndo ? meshEntry?.BeforeMesh : meshEntry?.AfterMesh;
            var beforeEntity = FindEntity(before.Document, entry.ObjectId);
            if (entry.SceneId != before.Document.SceneId ||
                beforeEntity is null ||
                (transformEntry is not null ? beforeEntity.Transform != expectedTransform :
                    meshEntry is null || beforeEntity.Mesh != expectedMesh))
            {
                return await FinishUncertainSceneEditResultAsync(
                    document,
                    before,
                    before.Document,
                    operationEditId,
                    "The scene no longer matches the pending history entry.")
                    .ConfigureAwait(false);
            }

            SceneDocumentOperationResult result;
            try
            {
                result = transformEntry is not null
                    ? await document.SetEntityTransformAsync(entry.ObjectId, targetTransform!.Value,
                        before.Document.Revision, CancellationToken.None).ConfigureAwait(false)
                    : await document.SetEntityMeshAsync(entry.ObjectId, targetMesh,
                        before.Document.Revision, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                return await RecoverFromUncertainOperationAsync(
                    document,
                    before,
                    operationEditId,
                    contentMayHaveChanged: true,
                    DiagnosticMessage(exception, "The scene history edit failed without a diagnostic."))
                    .ConfigureAwait(false);
            }

            if (!result.Succeeded)
            {
                return await FinishTypedSceneEditFailureAsync(
                    document,
                    before,
                    result,
                    operationEditId).ConfigureAwait(false);
            }

            var validReceipt = transformEntry is not null
                ? TryValidateTransformReceipt(before.Document, beforeEntity, entry.ObjectId,
                    targetTransform!.Value, before.Document.Revision, result, requireChanged: true, out _)
                : TryValidateMeshReceipt(before.Document, beforeEntity, entry.ObjectId,
                    targetMesh, before.Document.Revision, result, requireChanged: true, out _);
            if (!validReceipt)
            {
                return await FinishUncertainSceneEditResultAsync(
                    document,
                    before,
                    result.Current,
                    operationEditId,
                    "The successful scene history receipt did not match its entry and authoritative snapshot.")
                    .ConfigureAwait(false);
            }

            if (isUndo)
            {
                editHistory_.CommitUndo(entry);
            }
            else
            {
                editHistory_.CommitRedo(entry);
            }
            var currentContentStateId = isUndo
                ? entry.BeforeContentStateId
                : entry.AfterContentStateId;
            var next = SnapshotWithState(
                before,
                result.Current,
                currentContentStateId,
                before.SavedContentStateId);
            Publish(next, operationEditId, originatingEditSucceeded: true);
            return ProjectSessionOperationResult.Success(
                next,
                isUndo ? $"Undid {entry.Label}." : $"Redid {entry.Label}.",
                originatingEditId: operationEditId);
        }
        finally
        {
            operationGate_.Release();
        }
    }

    private async ValueTask<ProjectSessionOperationResult> FinishTypedSceneEditFailureAsync(
        ISceneDocumentConnection document,
        ProjectSessionSnapshot before,
        SceneDocumentOperationResult result,
        ProjectEditId editId)
    {
        if (result.Failure?.Kind == SceneDocumentFailureKind.AuthoritativeStateUnknown)
        {
            return await RecoverFromUncertainOperationAsync(
                document,
                before,
                editId,
                contentMayHaveChanged: true,
                result.Failure.Message).ConfigureAwait(false);
        }

        if (before.Document is null ||
            !IsSameDocument(before.Document, result.Current) ||
            result.Current.Revision != before.Document.Revision)
        {
            return await FinishUncertainSceneEditResultAsync(
                document,
                before,
                result.Current,
                editId,
                "The failed scene edit returned a mutated or inconsistent snapshot.")
                .ConfigureAwait(false);
        }

        var next = SnapshotWithDocument(before, result.Current);
        Publish(next, editId, originatingEditSucceeded: false);
        var failure = result.Failure!;
        return ProjectSessionOperationResult.Failed(
            next,
            MapSceneFailure(failure.Kind),
            failure.Message,
            editId);
    }

    private ProjectSessionOperationResult? ValidateDocumentTransition(
        ProjectDocumentTransitionExpectation expectation)
    {
        var current = Current;
        if (exitPrepared_)
        {
            return ExitPreparedFailure();
        }
        if (!expectation.Matches(current))
        {
            return ProjectSessionOperationResult.Failed(
                current,
                ProjectSessionFailureKind.StaleDocumentTransition,
                "The active document changed before the requested transition could commit.");
        }
        if (current.IsDirty && !expectation.AllowsUnsavedDiscard)
        {
            return ProjectSessionOperationResult.Failed(
                current,
                ProjectSessionFailureKind.StaleDocumentTransition,
                "The requested document transition did not authorize discarding unsaved content.");
        }
        return null;
    }

    private ProjectSessionOperationResult ExitPreparedFailure(
        ProjectEditId? originatingEditId = null) =>
        ProjectSessionOperationResult.Failed(
            Current,
            ProjectSessionFailureKind.Busy,
            "Studio shutdown has already committed; project mutations are no longer accepted.",
            originatingEditId);

    private async ValueTask<ProjectSessionOperationResult> FinishUncertainSceneEditResultAsync(
        ISceneDocumentConnection document,
        ProjectSessionSnapshot before,
        SceneDocumentSnapshot resultDocument,
        ProjectEditId editId,
        string message)
    {
        if (before.Document is null ||
            !IsSameDocument(before.Document, resultDocument) ||
            resultDocument.Revision < before.Document.Revision)
        {
            return await RecoverFromUncertainOperationAsync(
                document,
                before,
                editId,
                contentMayHaveChanged: true,
                message).ConfigureAwait(false);
        }

        var next = ResetHistoryAfterUncertainMutation(before, resultDocument);
        Publish(next, editId, originatingEditSucceeded: false);
        return ProjectSessionOperationResult.Failed(
            next,
            ProjectSessionFailureKind.InternalError,
            message,
            editId);
    }

    private async ValueTask<ProjectSessionOperationResult> RecoverFromUncertainOperationAsync(
        ISceneDocumentConnection document,
        ProjectSessionSnapshot before,
        ProjectEditId? editId,
        bool contentMayHaveChanged,
        string message)
    {
        SceneDocumentOperationResult? refresh = null;
        try
        {
            refresh = await document.RefreshAsync(CancellationToken.None).ConfigureAwait(false);
        }
        catch (Exception)
        {
            // A failed refresh cannot establish which native state was committed.
        }

        if (refresh?.Succeeded == true && before.Document is not null &&
            IsSameDocument(before.Document, refresh.Current) &&
            refresh.Current.Revision >= before.Document.Revision)
        {
            ProjectSessionSnapshot next;
            if (contentMayHaveChanged)
            {
                next = ResetHistoryAfterUncertainMutation(before, refresh.Current);
            }
            else
            {
                next = SnapshotWithDocument(before, refresh.Current);
            }
            Publish(next, editId, editId is null ? null : false);
            return ProjectSessionOperationResult.Failed(
                next,
                ProjectSessionFailureKind.InternalError,
                $"{message} The authoritative scene was refreshed; reopen the document if the result is unexpected.",
                editId);
        }

        editHistory_.Reset();
        activeDocument_ = null;
        try
        {
            await document.DisposeAsync().ConfigureAwait(false);
        }
        catch (Exception)
        {
            // The session is invalidated even if the broken connection cannot be disposed cleanly.
        }
        Publish(ProjectSessionSnapshot.NoProject, editId, editId is null ? null : false);
        return ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            ProjectSessionFailureKind.InternalError,
            $"{message} The authoritative scene could not be refreshed; reopen the project.",
            editId);
    }

    private ContentStateId AllocateContentStateId()
    {
        nextContentStateValue_ = checked(nextContentStateValue_ + 1);
        return new ContentStateId(nextContentStateValue_);
    }

    private ProjectSessionSnapshot ResetHistoryAfterChangedNonUndoableMutation(
        ProjectSessionSnapshot before,
        SceneDocumentSnapshot document)
    {
        editHistory_.Reset();
        return SnapshotWithState(
            before,
            document,
            AllocateContentStateId(),
            before.SavedContentStateId);
    }

    private ProjectSessionSnapshot ResetHistoryAfterUncertainMutation(
        ProjectSessionSnapshot before,
        SceneDocumentSnapshot? document = null)
    {
        editHistory_.Reset();
        return SnapshotWithState(
            before,
            IsSameDocument(before.Document, document) ? document! : before.Document!,
            AllocateContentStateId(),
            before.SavedContentStateId);
    }

    private ProjectSessionSnapshot SnapshotWithDocument(
        ProjectSessionSnapshot before,
        SceneDocumentSnapshot document) =>
        SnapshotWithState(
            before,
            document,
            before.CurrentContentStateId,
            before.SavedContentStateId);

    private ProjectSessionSnapshot SnapshotWithState(
        ProjectSessionSnapshot before,
        SceneDocumentSnapshot document,
        ContentStateId currentContentStateId,
        ContentStateId savedContentStateId)
    {
        return ProjectSessionSnapshot.Ready(
            before.Project!,
            document,
            currentContentStateId,
            savedContentStateId,
            editHistory_.CanUndo,
            editHistory_.CanRedo,
            editHistory_.UndoLabel,
            editHistory_.RedoLabel);
    }

    private static bool TryValidateTransformReceipt(
        SceneDocumentSnapshot beforeDocument,
        SceneEntitySnapshot? beforeEntity,
        Guid objectId,
        TransformValue requestedTransform,
        ulong expectedRevision,
        SceneDocumentOperationResult result,
        bool requireChanged,
        out SceneEntityTransformReceipt receipt)
    {
        receipt = result.TransformReceipt!;
        if (!result.Succeeded || receipt is null || beforeEntity is null ||
            expectedRevision != beforeDocument.Revision ||
            receipt.ObjectId != objectId ||
            receipt.BeforeRevision != expectedRevision ||
            receipt.BeforeTransform != beforeEntity.Transform ||
            receipt.AfterTransform != requestedTransform ||
            (requireChanged && !receipt.Changed) ||
            !IsSameDocument(beforeDocument, result.Current) ||
            result.Current.Revision != receipt.AfterRevision)
        {
            return false;
        }

        var afterEntity = FindEntity(result.Current, objectId);
        if (afterEntity is null || afterEntity.Transform != receipt.AfterTransform)
        {
            return false;
        }

        if (receipt.Changed)
        {
            return receipt.BeforeTransform != receipt.AfterTransform &&
                   receipt.AfterRevision == receipt.BeforeRevision + 1;
        }

        return receipt.BeforeTransform == receipt.AfterTransform &&
               receipt.BeforeRevision == receipt.AfterRevision &&
               result.Current.Revision == beforeDocument.Revision;
    }

    private static bool TryValidateMeshReceipt(
        SceneDocumentSnapshot beforeDocument,
        SceneEntitySnapshot? beforeEntity,
        Guid objectId,
        SceneMeshReference? requestedMesh,
        ulong expectedRevision,
        SceneDocumentOperationResult result,
        bool requireChanged,
        out SceneEntityMeshReceipt receipt)
    {
        receipt = result.MeshReceipt!;
        if (!result.Succeeded || receipt is null || beforeEntity is null ||
            expectedRevision != beforeDocument.Revision ||
            receipt.ObjectId != objectId ||
            receipt.BeforeRevision != expectedRevision ||
            receipt.BeforeMesh != beforeEntity.Mesh ||
            receipt.AfterMesh != requestedMesh ||
            (requireChanged && !receipt.Changed) ||
            !IsSameDocument(beforeDocument, result.Current) ||
            result.Current.Revision != receipt.AfterRevision)
        {
            return false;
        }

        var afterEntity = FindEntity(result.Current, objectId);
        if (afterEntity is null || afterEntity.Mesh != receipt.AfterMesh)
        {
            return false;
        }

        if (receipt.Changed)
        {
            return receipt.BeforeMesh != receipt.AfterMesh &&
                   receipt.AfterRevision == receipt.BeforeRevision + 1;
        }

        return receipt.BeforeMesh == receipt.AfterMesh &&
               receipt.BeforeRevision == receipt.AfterRevision &&
               result.Current.Revision == beforeDocument.Revision;
    }

    private static SceneEntitySnapshot? FindEntity(
        SceneDocumentSnapshot snapshot,
        Guid objectId)
    {
        SceneEntitySnapshot? match = null;
        foreach (var entity in snapshot.Entities)
        {
            if (entity.ObjectId != objectId)
            {
                continue;
            }
            if (match is not null)
            {
                return null;
            }
            match = entity;
        }
        return match;
    }

    private static bool IsSameDocument(
        SceneDocumentSnapshot? before,
        SceneDocumentSnapshot? after) =>
        before is not null && after is not null &&
        before.SceneId == after.SceneId &&
        string.Equals(before.Path, after.Path, StringComparison.Ordinal);

    private static string TransformLabel(SceneEntitySnapshot? entity) =>
        entity is null || string.IsNullOrWhiteSpace(entity.Name)
            ? "Edit Transform"
            : $"Edit Transform '{entity.Name}'";

    private static string MeshLabel(SceneEntitySnapshot? entity) =>
        entity is null || string.IsNullOrWhiteSpace(entity.Name)
            ? "Edit Mesh"
            : $"Edit Mesh '{entity.Name}'";

    private void Publish(
        ProjectSessionSnapshot snapshot,
        ProjectEditId? originatingEditId = null,
        bool? originatingEditSucceeded = null)
    {
        lock (snapshotGate_)
        {
            current_ = snapshot;
        }
        SnapshotChanged?.Invoke(
            this,
            new ProjectSessionSnapshotChangedEventArgs(
                snapshot,
                originatingEditId,
                originatingEditSucceeded));
    }

    private CancellationTokenSource CreateLinkedCancellation(CancellationToken cancellationToken) =>
        CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            lifetimeCancellation_.Token);

    private void ThrowIfDisposed() =>
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposeStarted_) != 0, this);

    private static string DiagnosticMessage(Exception exception, string fallback) =>
        string.IsNullOrWhiteSpace(exception.Message) ? fallback : exception.Message;

    private static bool ContainsCreatedObject(
        SceneDocumentSnapshot snapshot,
        Guid objectId,
        SceneMeshReference? expectedMesh)
    {
        SceneEntitySnapshot? match = null;
        foreach (var entity in snapshot.Entities)
        {
            if (entity.ObjectId != objectId)
            {
                continue;
            }
            if (match is not null)
            {
                return false;
            }
            match = entity;
        }
        return match is not null && match.Mesh == expectedMesh;
    }

    private static ProjectSessionFailureKind MapProjectFailure(
        ProjectDescriptorFailureKind kind) => kind switch
        {
            ProjectDescriptorFailureKind.InvalidInput => ProjectSessionFailureKind.InvalidInput,
            ProjectDescriptorFailureKind.InvalidProject => ProjectSessionFailureKind.InvalidProject,
            ProjectDescriptorFailureKind.AlreadyExists => ProjectSessionFailureKind.AlreadyExists,
            ProjectDescriptorFailureKind.Busy => ProjectSessionFailureKind.Busy,
            ProjectDescriptorFailureKind.IoFailure => ProjectSessionFailureKind.IoFailure,
            ProjectDescriptorFailureKind.NativeUnavailable => ProjectSessionFailureKind.NativeUnavailable,
            _ => ProjectSessionFailureKind.InternalError,
        };

    private static ProjectSessionFailureKind MapSceneFailure(
        SceneDocumentFailureKind kind) => kind switch
        {
            SceneDocumentFailureKind.InvalidInput => ProjectSessionFailureKind.InvalidInput,
            SceneDocumentFailureKind.InvalidScene => ProjectSessionFailureKind.InvalidScene,
            SceneDocumentFailureKind.RevisionConflict => ProjectSessionFailureKind.RevisionConflict,
            SceneDocumentFailureKind.InvalidObject => ProjectSessionFailureKind.InvalidObject,
            SceneDocumentFailureKind.InvalidTransform => ProjectSessionFailureKind.InvalidTransform,
            SceneDocumentFailureKind.InvalidAssetReference =>
                ProjectSessionFailureKind.InvalidAssetReference,
            SceneDocumentFailureKind.RevisionExhausted =>
                ProjectSessionFailureKind.InternalError,
            SceneDocumentFailureKind.IoFailure => ProjectSessionFailureKind.IoFailure,
            SceneDocumentFailureKind.NativeUnavailable => ProjectSessionFailureKind.NativeUnavailable,
            SceneDocumentFailureKind.AuthoritativeStateUnknown =>
                ProjectSessionFailureKind.InternalError,
            _ => ProjectSessionFailureKind.InternalError,
        };

    private enum SceneOperationKind
    {
        NonUndoableMutation,
        Save,
    }
}

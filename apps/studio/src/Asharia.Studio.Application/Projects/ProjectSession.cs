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
    private ProjectSessionSnapshot current_ = ProjectSessionSnapshot.NoProject;
    private ISceneDocumentConnection? activeDocument_;
    private int disposeStarted_;

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
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(parentDirectory);
        ArgumentException.ThrowIfNullOrWhiteSpace(projectName);
        return OpenProjectCoreAsync(
            token => projectGateway_.CreateMinimalProjectAsync(
                parentDirectory,
                projectName,
                Guid.NewGuid(),
                token),
            descriptor => $"Created project '{descriptor.ProjectName}' and opened its default scene.",
            cancellationToken);
    }

    public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectPath);
        return OpenProjectCoreAsync(
            token => projectGateway_.OpenProjectAsync(projectPath, token),
            descriptor => $"Opened project '{descriptor.ProjectName}' and its default scene.",
            cancellationToken);
    }

    public async ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
        CancellationToken cancellationToken = default)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
            var document = activeDocument_;
            if (document is null)
            {
                return ProjectSessionOperationResult.Success(
                    Current,
                    "No project is open.");
            }

            await document.DisposeAsync().ConfigureAwait(false);
            activeDocument_ = null;
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
            expectedMesh: null);
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
            mesh);
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
            cancellationToken);
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
        return EditSceneAsync(
            (document, _) => document.SetEntityTransformAsync(
                objectId,
                transform,
                context.ExpectedRevision,
                CancellationToken.None),
            "Updated the scene entity Transform.",
            cancellationToken,
            originatingEditId: context.EditId);
    }

    public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default) =>
        EditSceneAsync(
            (document, snapshot) => document.SaveAsync(
                snapshot.Revision,
                CancellationToken.None),
            "Saved the active scene.",
            cancellationToken);

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
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
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
            var next = ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    ProjectSessionId.CreateNew(),
                    descriptor.ProjectId,
                    descriptor.ProjectName,
                    descriptor.RootPath),
                sceneResult.Document!);
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
        ProjectEditId? originatingEditId = null)
    {
        ThrowIfDisposed();
        using var linkedCancellation = CreateLinkedCancellation(cancellationToken);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            linkedCancellation.Token.ThrowIfCancellationRequested();
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
                return ProjectSessionOperationResult.Failed(
                    Current,
                    ProjectSessionFailureKind.InternalError,
                    DiagnosticMessage(exception, "The scene edit failed without a diagnostic."),
                    originatingEditId);
            }

            var next = ProjectSessionSnapshot.Ready(before.Project, result.Current);
            Publish(
                next,
                originatingEditId,
                originatingEditId is null ? null : result.Succeeded);
            if (!result.Succeeded)
            {
                var failure = result.Failure!;
                return ProjectSessionOperationResult.Failed(
                    next,
                    MapSceneFailure(failure.Kind),
                    failure.Message,
                    originatingEditId);
            }
            if (createdObjectId is Guid createdId &&
                !ContainsCreatedObject(result.Current, createdId, expectedMesh))
            {
                return ProjectSessionOperationResult.Failed(
                    next,
                    ProjectSessionFailureKind.InternalError,
                    "The successful scene create receipt is absent from the " +
                    "authoritative snapshot.",
                    originatingEditId);
            }
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
            SceneDocumentFailureKind.IoFailure => ProjectSessionFailureKind.IoFailure,
            SceneDocumentFailureKind.NativeUnavailable => ProjectSessionFailureKind.NativeUnavailable,
            _ => ProjectSessionFailureKind.InternalError,
        };
}

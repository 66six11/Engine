using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Editor.Shell.Services.Projects;
using Editor.Shell.ViewModels.Windowing;

namespace Asharia.Studio.TestSupport;

internal static class StudioShellTestFactory
{
    public static StudioShellViewModel Create() =>
        Create(out _, out _);

    public static StudioShellViewModel Create(
        out TestProjectSession projectSession,
        out TestProjectDialogService projectDialogs)
    {
        projectSession = new TestProjectSession();
        projectDialogs = new TestProjectDialogService();
        return new StudioShellViewModel(
            projectSession,
            projectDialogs,
            CreateDocumentTransitions(projectSession),
            CreateDiagnosticWriter(),
            new TestProjectAssetCatalog(),
            new TestEditorSelectionService());
    }

    public static ProjectDocumentTransitionCoordinator CreateDocumentTransitions(
        IProjectSession projectSession) =>
        new(projectSession, new TestProjectDocumentTransitionPrompt());

    public static StudioOperationDiagnosticWriter CreateDiagnosticWriter() =>
        CreateDiagnosticWriter(out _);

    public static StudioOperationDiagnosticWriter CreateDiagnosticWriter(
        out StudioDiagnosticHub hub)
    {
        hub = new StudioDiagnosticHub(diagnosticCapacity: 64, logCapacity: 16);
        return new StudioOperationDiagnosticWriter(hub);
    }

    public static TestProjectAssetCatalog CreateProjectAssetCatalog() => new();

    public static TestEditorSelectionService CreateEditorSelectionService() => new();
}

internal sealed class TestEditorSelectionService : IEditorSelectionService
{
    public event EventHandler<EditorSelectionChangedEventArgs>? Changed;

    public EditorSelectionSnapshot Current { get; private set; } =
        new(0, primary: null, EditorSelectionChangeReason.Initialization);

    public int DisposeCount { get; private set; }

    public Exception? DisposeException { get; set; }

    public Action? DisposeHandler { get; set; }

    public bool Replace(
        EditorSelectionTarget target,
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User)
    {
        ArgumentNullException.ThrowIfNull(target);
        if (Equals(Current.Primary, target))
        {
            return false;
        }

        Publish(target, reason);
        return true;
    }

    public bool Clear(
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User)
    {
        if (Current.Primary is null)
        {
            return false;
        }

        Publish(primary: null, reason);
        return true;
    }

    public void Dispose()
    {
        DisposeCount++;
        DisposeHandler?.Invoke();
        if (DisposeException is not null)
        {
            throw DisposeException;
        }
        Changed = null;
    }

    private void Publish(EditorSelectionTarget? primary, EditorSelectionChangeReason reason)
    {
        Current = new EditorSelectionSnapshot(
            checked(Current.Revision + 1),
            primary,
            reason);
        Changed?.Invoke(this, new EditorSelectionChangedEventArgs(Current));
    }
}

internal sealed class TestProjectAssetCatalog : IProjectAssetCatalog
{
    public event EventHandler<AssetCatalogSessionSnapshotChangedEventArgs>? SnapshotChanged;

    public AssetCatalogSessionSnapshot Current { get; private set; } =
        AssetCatalogSessionSnapshot.NoProject();

    public int RefreshCount { get; private set; }

    public int DisposeCount { get; private set; }

    public Exception? DisposeException { get; set; }

    public Action? DisposeHandler { get; set; }

    public ValueTask RefreshAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        RefreshCount++;
        return ValueTask.CompletedTask;
    }

    public ValueTask DisposeAsync()
    {
        DisposeCount++;
        DisposeHandler?.Invoke();
        if (DisposeException is not null)
        {
            return ValueTask.FromException(DisposeException);
        }
        return ValueTask.CompletedTask;
    }

    public void Publish(AssetCatalogSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        Current = snapshot;
        SnapshotChanged?.Invoke(
            this,
            new AssetCatalogSessionSnapshotChangedEventArgs(snapshot));
    }
}

internal sealed class TestProjectDocumentTransitionPrompt :
    IProjectDocumentTransitionPrompt
{
    private readonly List<ProjectDocumentTransitionPrompt> requests_ = [];

    public ProjectDocumentTransitionDecision Decision { get; set; } =
        ProjectDocumentTransitionDecision.Discard;

    public Func<ProjectDocumentTransitionPrompt, CancellationToken,
        ValueTask<ProjectDocumentTransitionDecision>>? Handler
    { get; set; }

    public IReadOnlyList<ProjectDocumentTransitionPrompt> Requests => requests_;

    public int RequestCount => requests_.Count;

    public ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
        ProjectDocumentTransitionPrompt prompt,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        cancellationToken.ThrowIfCancellationRequested();
        requests_.Add(prompt);
        return Handler?.Invoke(prompt, cancellationToken)
            ?? ValueTask.FromResult(Decision);
    }
}

internal sealed class TestProjectSession : IProjectSession
{
    public event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;

    public ProjectSessionSnapshot Current { get; private set; } =
        ProjectSessionSnapshot.NoProject;

    public Func<string, string, ProjectDocumentTransitionExpectation, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? CreateHandler
    { get; set; }

    public Func<string, ProjectDocumentTransitionExpectation, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? OpenHandler
    { get; set; }

    public Func<ProjectDocumentTransitionExpectation, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? CloseHandler
    { get; set; }

    public Func<ProjectDocumentTransitionExpectation, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? PrepareExitHandler
    { get; set; }

    public Func<string, CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        CreateEntityHandler
    { get; set; }

    public Func<string, SceneMeshReference, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? CreateMeshEntityHandler
    { get; set; }

    public Func<Guid, string, CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        SetNameHandler
    { get; set; }

    public Func<Guid, TransformValue, ProjectSessionEditContext, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? SetTransformHandler
    { get; set; }

    public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        SaveHandler
    { get; set; }

    public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        UndoHandler
    { get; set; }

    public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        RedoHandler
    { get; set; }

    public int DisposeCount { get; private set; }

    public Exception? DisposeException { get; set; }

    public Action? DisposeHandler { get; set; }

    public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
        string parentDirectory,
        string projectName,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default) =>
        CreateHandler?.Invoke(parentDirectory, projectName, expectation, cancellationToken)
        ?? throw new InvalidOperationException("No create-project result was configured.");

    public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default) =>
        OpenHandler?.Invoke(projectPath, expectation, cancellationToken)
        ?? throw new InvalidOperationException("No open-project result was configured.");

    public ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default) =>
        CloseHandler?.Invoke(expectation, cancellationToken)
        ?? throw new InvalidOperationException("No close-project result was configured.");

    public ValueTask<ProjectSessionOperationResult> PrepareExitAsync(
        ProjectDocumentTransitionExpectation expectation,
        CancellationToken cancellationToken = default) =>
        PrepareExitHandler?.Invoke(expectation, cancellationToken)
        ?? throw new InvalidOperationException("No prepare-exit result was configured.");

    public ValueTask<ProjectSessionOperationResult> CreateEntityAsync(
        string name,
        CancellationToken cancellationToken = default) =>
        CreateEntityHandler?.Invoke(name, cancellationToken)
        ?? throw new InvalidOperationException("No create-entity result was configured.");

    public ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(
        string name,
        SceneMeshReference mesh,
        CancellationToken cancellationToken = default) =>
        CreateMeshEntityHandler?.Invoke(name, mesh, cancellationToken)
        ?? throw new InvalidOperationException("No create-mesh-entity result was configured.");

    public ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(
        Guid objectId,
        string name,
        CancellationToken cancellationToken = default) =>
        SetNameHandler?.Invoke(objectId, name, cancellationToken)
        ?? throw new InvalidOperationException("No set-name result was configured.");

    public ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(
        Guid objectId,
        TransformValue transform,
        ProjectSessionEditContext context,
        CancellationToken cancellationToken = default) =>
        SetTransformHandler?.Invoke(
            objectId,
            transform,
            context,
            cancellationToken)
        ?? throw new InvalidOperationException("No set-Transform result was configured.");

    public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default) =>
        SaveHandler?.Invoke(cancellationToken)
        ?? throw new InvalidOperationException("No save-scene result was configured.");

    public ValueTask<ProjectSessionOperationResult> UndoAsync(
        CancellationToken cancellationToken = default) =>
        UndoHandler?.Invoke(cancellationToken)
        ?? throw new InvalidOperationException("No Undo result was configured.");

    public ValueTask<ProjectSessionOperationResult> RedoAsync(
        CancellationToken cancellationToken = default) =>
        RedoHandler?.Invoke(cancellationToken)
        ?? throw new InvalidOperationException("No Redo result was configured.");

    public ValueTask DisposeAsync()
    {
        DisposeCount++;
        DisposeHandler?.Invoke();
        if (DisposeException is not null)
        {
            return ValueTask.FromException(DisposeException);
        }
        return ValueTask.CompletedTask;
    }

    public void Publish(
        ProjectSessionSnapshot snapshot,
        ProjectEditId? originatingEditId = null,
        bool? originatingEditSucceeded = null)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (originatingEditId is not null && originatingEditSucceeded is null)
        {
            throw new ArgumentException(
                "A test edit publication must state whether the edit succeeded.",
                nameof(originatingEditSucceeded));
        }
        Current = snapshot;
        SnapshotChanged?.Invoke(
            this,
            new ProjectSessionSnapshotChangedEventArgs(
                snapshot,
                originatingEditId,
                originatingEditSucceeded));
    }

}

internal sealed class TestProjectDialogService : IStudioProjectDialogService
{
    public string? ParentDirectory { get; set; }

    public string? ProjectDescriptor { get; set; }

    public ValueTask<string?> SelectProjectParentDirectoryAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ParentDirectory);
    }

    public ValueTask<string?> SelectProjectDescriptorAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ProjectDescriptor);
    }
}

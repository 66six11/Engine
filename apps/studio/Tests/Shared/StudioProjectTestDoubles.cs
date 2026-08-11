using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
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
        return new StudioShellViewModel(projectSession, projectDialogs);
    }
}

internal sealed class TestProjectSession : IProjectSession
{
    public event EventHandler? SnapshotChanged;

    public ProjectSessionSnapshot Current { get; private set; } =
        ProjectSessionSnapshot.NoProject;

    public Func<string, string, CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        CreateHandler
    { get; set; }

    public Func<string, CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        OpenHandler
    { get; set; }

    public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        CloseHandler
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

    public Func<Guid, TransformValue, CancellationToken,
        ValueTask<ProjectSessionOperationResult>>? SetTransformHandler
    { get; set; }

    public Func<CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        SaveHandler
    { get; set; }

    public int DisposeCount { get; private set; }

    public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
        string parentDirectory,
        string projectName,
        CancellationToken cancellationToken = default) =>
        CreateHandler?.Invoke(parentDirectory, projectName, cancellationToken)
        ?? throw new InvalidOperationException("No create-project result was configured.");

    public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default) =>
        OpenHandler?.Invoke(projectPath, cancellationToken)
        ?? throw new InvalidOperationException("No open-project result was configured.");

    public ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
        CancellationToken cancellationToken = default) =>
        CloseHandler?.Invoke(cancellationToken)
        ?? throw new InvalidOperationException("No close-project result was configured.");

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
        CancellationToken cancellationToken = default) =>
        SetTransformHandler?.Invoke(objectId, transform, cancellationToken)
        ?? throw new InvalidOperationException("No set-Transform result was configured.");

    public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
        CancellationToken cancellationToken = default) =>
        SaveHandler?.Invoke(cancellationToken)
        ?? throw new InvalidOperationException("No save-scene result was configured.");

    public ValueTask DisposeAsync()
    {
        DisposeCount++;
        return ValueTask.CompletedTask;
    }

    public void Publish(ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        Current = snapshot;
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
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

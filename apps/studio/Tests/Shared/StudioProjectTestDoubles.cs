using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
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
        CreateHandler { get; set; }

    public Func<string, CancellationToken, ValueTask<ProjectSessionOperationResult>>?
        OpenHandler { get; set; }

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

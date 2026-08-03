using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Projects;

public sealed class ProjectSession : IProjectSession
{
    private readonly IProjectDescriptorGateway gateway_;
    private readonly SemaphoreSlim operationGate_ = new(1, 1);
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly object snapshotGate_ = new();
    private ProjectSessionSnapshot current_ = ProjectSessionSnapshot.NoProject;
    private int disposeStarted_;

    public ProjectSession(IProjectDescriptorGateway gateway)
    {
        ArgumentNullException.ThrowIfNull(gateway);
        gateway_ = gateway;
    }

    public event EventHandler? SnapshotChanged;

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
        return ExecuteAsync(
            token => gateway_.CreateMinimalProjectAsync(
                parentDirectory,
                projectName,
                Guid.NewGuid(),
                token),
            snapshot => $"Created project '{snapshot.ProjectName}'.",
            cancellationToken);
    }

    public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectPath);
        return ExecuteAsync(
            token => gateway_.OpenProjectAsync(projectPath, token),
            snapshot => $"Opened project '{snapshot.ProjectName}'.",
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposeStarted_, 1) != 0)
        {
            return;
        }

        await lifetimeCancellation_.CancelAsync().ConfigureAwait(false);
        await operationGate_.WaitAsync().ConfigureAwait(false);
        operationGate_.Release();
    }

    private async ValueTask<ProjectSessionOperationResult> ExecuteAsync(
        Func<CancellationToken, ValueTask<ProjectDescriptorOperationResult>> operation,
        Func<ProjectDescriptorSnapshot, string> successMessage,
        CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(
            Volatile.Read(ref disposeStarted_) != 0,
            this);
        using var linkedCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            lifetimeCancellation_.Token);
        await operationGate_.WaitAsync(linkedCancellation.Token).ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(
                Volatile.Read(ref disposeStarted_) != 0,
                this);

            ProjectDescriptorOperationResult result;
            try
            {
                result = await operation(linkedCancellation.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception)
            {
                result = ProjectDescriptorOperationResult.Failed(
                    new ProjectDescriptorFailure(
                        ProjectDescriptorFailureKind.InternalError,
                        string.IsNullOrWhiteSpace(exception.Message)
                            ? "The project adapter failed without a diagnostic."
                            : exception.Message));
            }

            linkedCancellation.Token.ThrowIfCancellationRequested();
            if (!result.Succeeded)
            {
                return ProjectSessionOperationResult.Failed(
                    Current,
                    result.Failure!);
            }

            var descriptor = result.Project!;
            var next = ProjectSessionSnapshot.Ready(
                new ActiveProjectSnapshot(
                    ProjectSessionId.CreateNew(),
                    descriptor.ProjectId,
                    descriptor.ProjectName,
                    descriptor.RootPath));
            lock (snapshotGate_)
            {
                current_ = next;
            }
            SnapshotChanged?.Invoke(this, EventArgs.Empty);
            return ProjectSessionOperationResult.Success(
                next,
                successMessage(descriptor));
        }
        finally
        {
            operationGate_.Release();
        }
    }
}

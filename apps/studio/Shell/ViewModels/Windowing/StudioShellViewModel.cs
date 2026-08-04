using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Studio.Application.Projects;
using Avalonia.Threading;
using Editor.Shell.Commands;
using Editor.Shell.Services.Projects;

namespace Editor.Shell.ViewModels.Windowing;

internal enum StudioShellStage
{
    Starting,
    Ready,
    Stopping,
}

internal sealed class StudioShellViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly IProjectSession projectSession_;
    private readonly IStudioProjectDialogService projectDialogs_;
    private readonly AsyncCommand createProjectCommand_;
    private readonly AsyncCommand openProjectCommand_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private StudioShellStage stage_ = StudioShellStage.Starting;
    private ProjectSessionSnapshot projectSnapshot_;
    private string newProjectName_ = "MyProject";
    private string projectOperationMessage_ = string.Empty;
    private bool isProjectOperationRunning_;
    private bool isDisposed_;

    public StudioShellViewModel(
        IProjectSession projectSession,
        IStudioProjectDialogService projectDialogs)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(projectDialogs);
        projectSession_ = projectSession;
        projectDialogs_ = projectDialogs;
        projectSnapshot_ = projectSession.Current;
        createProjectCommand_ = new AsyncCommand(CreateProjectAsync, CanCreateProject);
        openProjectCommand_ = new AsyncCommand(OpenProjectAsync, CanRunProjectOperation);
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public StudioShellStage Stage => stage_;

    public string WindowTitle
    {
        get
        {
            if (stage_ == StudioShellStage.Starting)
            {
                return "Starting - Asharia Studio";
            }

            var project = projectSnapshot_.Project?.ProjectName ?? "No Project";
            return $"No Document - {project} - Asharia Studio";
        }
    }

    public bool IsStarting => stage_ == StudioShellStage.Starting;

    public bool IsWorkspaceVisible => stage_ == StudioShellStage.Ready;

    public bool HasProject => projectSnapshot_.IsReady;

    public bool HasNoProject => !projectSnapshot_.IsReady;

    public string StartingStateText => "Starting";

    public string ProjectStateText =>
        projectSnapshot_.Project?.ProjectName ?? "No Project";

    public string ProjectPathText =>
        projectSnapshot_.Project?.RootPath ?? string.Empty;

    public string DocumentStateText => "No Document";

    public string NewProjectName
    {
        get => newProjectName_;
        set
        {
            if (string.Equals(newProjectName_, value, StringComparison.Ordinal))
            {
                return;
            }

            newProjectName_ = value;
            OnPropertyChanged();
            createProjectCommand_.RaiseCanExecuteChanged();
        }
    }

    public bool IsProjectOperationRunning
    {
        get => isProjectOperationRunning_;
        private set
        {
            if (isProjectOperationRunning_ == value)
            {
                return;
            }

            isProjectOperationRunning_ = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(ProjectOperationStateText));
            RaiseProjectCommandStateChanged();
        }
    }

    public string ProjectOperationStateText =>
        IsProjectOperationRunning ? "Working..." : string.Empty;

    public string ProjectOperationMessage
    {
        get => projectOperationMessage_;
        private set
        {
            if (string.Equals(projectOperationMessage_, value, StringComparison.Ordinal))
            {
                return;
            }

            projectOperationMessage_ = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasProjectOperationMessage));
        }
    }

    public bool HasProjectOperationMessage =>
        !string.IsNullOrWhiteSpace(ProjectOperationMessage);

    public ICommand CreateProjectCommand => createProjectCommand_;

    public ICommand OpenProjectCommand => openProjectCommand_;

    internal IProjectSession ProjectSession => projectSession_;

    public void MarkReady()
    {
        ThrowIfDisposed();
        if (stage_ != StudioShellStage.Starting)
        {
            throw new InvalidOperationException(
                $"Studio shell cannot enter Ready from '{stage_}'.");
        }

        SetStage(StudioShellStage.Ready);
    }

    public void MarkStopping()
    {
        ThrowIfDisposed();
        if (stage_ == StudioShellStage.Stopping)
        {
            return;
        }

        _ = lifetimeCancellation_.CancelAsync();
        SetStage(StudioShellStage.Stopping);
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
        lifetimeCancellation_.Cancel();
        RaiseProjectCommandStateChanged();
    }

    private bool CanCreateProject() =>
        CanRunProjectOperation() && !string.IsNullOrWhiteSpace(NewProjectName);

    private bool CanRunProjectOperation() =>
        !isDisposed_
        && stage_ == StudioShellStage.Ready
        && !IsProjectOperationRunning;

    private async Task CreateProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var parent = await projectDialogs_.SelectProjectParentDirectoryAsync(token);
            return parent is null
                ? null
                : await projectSession_.CreateProjectAsync(parent, NewProjectName, token);
        });
    }

    private async Task OpenProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var descriptor = await projectDialogs_.SelectProjectDescriptorAsync(token);
            return descriptor is null
                ? null
                : await projectSession_.OpenProjectAsync(descriptor, token);
        });
    }

    private async Task RunProjectOperationAsync(
        Func<CancellationToken, ValueTask<ProjectSessionOperationResult?>> operation)
    {
        ProjectOperationMessage = string.Empty;
        IsProjectOperationRunning = true;
        try
        {
            var result = await operation(lifetimeCancellation_.Token);
            if (result is null)
            {
                return;
            }

            ApplyProjectSnapshot(result.Current);
            ProjectOperationMessage = result.Message;
        }
        catch (OperationCanceledException) when (lifetimeCancellation_.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            ProjectOperationMessage = string.IsNullOrWhiteSpace(exception.Message)
                ? "The project operation failed without a diagnostic."
                : exception.Message;
        }
        finally
        {
            IsProjectOperationRunning = false;
        }
    }

    private void OnProjectSnapshotChanged(object? sender, EventArgs e)
    {
        var snapshot = projectSession_.Current;
        if (Dispatcher.UIThread.CheckAccess())
        {
            ApplyProjectSnapshot(snapshot);
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (!isDisposed_)
            {
                ApplyProjectSnapshot(snapshot);
            }
        });
    }

    private void ApplyProjectSnapshot(ProjectSessionSnapshot snapshot)
    {
        projectSnapshot_ = snapshot;
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(HasProject));
        OnPropertyChanged(nameof(HasNoProject));
        OnPropertyChanged(nameof(ProjectStateText));
        OnPropertyChanged(nameof(ProjectPathText));
    }

    private void SetStage(StudioShellStage stage)
    {
        stage_ = stage;
        OnPropertyChanged(nameof(Stage));
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(IsStarting));
        OnPropertyChanged(nameof(IsWorkspaceVisible));
        RaiseProjectCommandStateChanged();
    }

    private void RaiseProjectCommandStateChanged()
    {
        createProjectCommand_.RaiseCanExecuteChanged();
        openProjectCommand_.RaiseCanExecuteChanged();
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(isDisposed_, this);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

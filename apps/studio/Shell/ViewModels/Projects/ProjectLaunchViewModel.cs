using System;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Editor.Core.Abstractions;
using Editor.Core.Services;
using Editor.UI.Presentation;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Projects;

public sealed class ProjectLaunchViewModel : ViewModelBase, IDisposable
{
    private readonly IProjectOpenSessionSnapshotSource projectOpenSessions_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private ProjectOpenSessionSnapshot snapshot_;
    private bool isDisposed_;

    internal ProjectLaunchViewModel(
        IProjectOpenSessionSnapshotSource projectOpenSessions,
        IEditorUiDispatcher? uiDispatcher = null)
    {
        ArgumentNullException.ThrowIfNull(projectOpenSessions);

        projectOpenSessions_ = projectOpenSessions;
        uiDispatcher_ = uiDispatcher ?? new ImmediateEditorUiDispatcher();
        snapshot_ = projectOpenSessions_.Current;
        projectOpenSessions_.SnapshotChanged += OnSnapshotChanged;
    }

    public string ProjectCandidateDisplayName =>
        ProjectOpenSessionText.GetProjectDisplayName(snapshot_);

    public string StateLabel =>
        ProjectOpenSessionText.GetStateLabel(snapshot_.State);

    public string StateTitle =>
        ProjectOpenSessionText.GetStateTitle(snapshot_.State);

    public string StateMessage =>
        ProjectOpenSessionText.GetStateMessage(snapshot_.State);

    public string NextStepText =>
        $"Next: {ProjectOpenSessionText.GetNextActionLabel(snapshot_.NextAction)}";

    public bool HasDiagnostics => snapshot_.Diagnostics.Count > 0;

    public string DiagnosticCountText =>
        snapshot_.Diagnostics.Count == 1
            ? "1 project-open diagnostic"
            : $"{snapshot_.Diagnostics.Count} project-open diagnostics";

    public string PrimaryDiagnosticCode =>
        GetPrimaryDiagnostic()?.Code ?? string.Empty;

    public string PrimaryDiagnosticMessage =>
        GetPrimaryDiagnostic()?.Message ?? string.Empty;

    public string PrimaryDiagnosticManifestPath =>
        GetPrimaryDiagnostic()?.ManifestPath ?? string.Empty;

    public string PrimaryDiagnosticPointer =>
        GetPrimaryDiagnostic()?.Pointer ?? string.Empty;

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        projectOpenSessions_.SnapshotChanged -= OnSnapshotChanged;
    }

    private ProjectOpenSessionDiagnosticSnapshot? GetPrimaryDiagnostic() =>
        snapshot_.Diagnostics.Count == 0 ? null : snapshot_.Diagnostics[0];

    private void OnSnapshotChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshSnapshot();
            return;
        }

        uiDispatcher_.Post(RefreshSnapshot);
    }

    private void RefreshSnapshot()
    {
        var nextSnapshot = projectOpenSessions_.Current;
        if (ReferenceEquals(snapshot_, nextSnapshot))
        {
            return;
        }

        snapshot_ = nextSnapshot;
        OnPropertyChanged(nameof(ProjectCandidateDisplayName));
        OnPropertyChanged(nameof(StateLabel));
        OnPropertyChanged(nameof(StateTitle));
        OnPropertyChanged(nameof(StateMessage));
        OnPropertyChanged(nameof(NextStepText));
        OnPropertyChanged(nameof(HasDiagnostics));
        OnPropertyChanged(nameof(DiagnosticCountText));
        OnPropertyChanged(nameof(PrimaryDiagnosticCode));
        OnPropertyChanged(nameof(PrimaryDiagnosticMessage));
        OnPropertyChanged(nameof(PrimaryDiagnosticManifestPath));
        OnPropertyChanged(nameof(PrimaryDiagnosticPointer));
    }
}

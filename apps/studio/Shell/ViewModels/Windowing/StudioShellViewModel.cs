using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Editor.Shell.ViewModels.Windowing;

internal enum StudioShellStage
{
    Starting,
    Ready,
    Stopping,
}

internal sealed class StudioShellViewModel : INotifyPropertyChanged, IDisposable
{
    private StudioShellStage stage_ = StudioShellStage.Starting;
    private bool isDisposed_;

    public event PropertyChangedEventHandler? PropertyChanged;

    public StudioShellStage Stage => stage_;

    public string WindowTitle => stage_ == StudioShellStage.Starting
        ? "Starting — Asharia Studio"
        : "No Document — No Project — Asharia Studio";

    public bool IsStarting => stage_ == StudioShellStage.Starting;

    public bool IsWorkspaceEmpty => stage_ == StudioShellStage.Ready;

    public string StartingStateText => "Starting";

    public string ProjectStateText => "No Project";

    public string DocumentStateText => "No Document";

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

        SetStage(StudioShellStage.Stopping);
    }

    public void Dispose()
    {
        isDisposed_ = true;
    }

    private void SetStage(StudioShellStage stage)
    {
        stage_ = stage;
        OnPropertyChanged(nameof(Stage));
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(IsStarting));
        OnPropertyChanged(nameof(IsWorkspaceEmpty));
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

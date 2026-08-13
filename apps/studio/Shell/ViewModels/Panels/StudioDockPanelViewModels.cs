using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.ViewModels.Panels;

internal abstract class StudioDockPanelViewModel
{
    protected StudioDockPanelViewModel(StudioShellViewModel shell)
    {
        ArgumentNullException.ThrowIfNull(shell);
        Shell = shell;
    }

    public StudioShellViewModel Shell { get; }
}

internal sealed class StudioScenePanelViewModel :
    StudioDockPanelViewModel,
    INotifyPropertyChanged,
    IDisposable
{
    private readonly IProjectSession projectSession_;
    private ViewportSession? session_;
    private ulong viewportRevision_;
    private bool isRealtime_ = true;
    private bool isWireframe_;
    private bool isDisposed_;

    public StudioScenePanelViewModel(StudioShellViewModel shell)
        : base(shell)
    {
        projectSession_ = shell.ProjectSession;
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
        ApplyProjectSnapshot(projectSession_.Current);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ViewportSession? Session => session_;

    public ulong ViewportRevision => viewportRevision_;

    public bool IsRealtime
    {
        get => isRealtime_;
        set
        {
            if (isRealtime_ == value)
            {
                return;
            }
            isRealtime_ = value;
            OnPropertyChanged();
        }
    }

    public bool IsWireframe
    {
        get => isWireframe_;
        set
        {
            if (isWireframe_ == value)
            {
                return;
            }
            isWireframe_ = value;
            session_?.SetSceneRasterMode(
                value
                    ? ViewportSceneRasterMode.Wireframe
                    : ViewportSceneRasterMode.Solid);
            OnPropertyChanged();
        }
    }

    public ViewportPresentationLifetime PresentationLifetime =>
        Shell.ViewportPresentationLifetime;

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
        session_?.Close();
        session_ = null;
    }

    private void OnProjectSnapshotChanged(
        object? sender,
        ProjectSessionSnapshotChangedEventArgs e)
    {
        var snapshot = e.Snapshot;
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
        var document = snapshot.Document;
        if (document is null)
        {
            ReplaceSession(null);
            return;
        }

        if (session_ is { } session && session.Current.TargetId == document.SceneId)
        {
            session.SynchronizeDocument(document);
            viewportRevision_ = document.Revision;
            OnPropertyChanged(nameof(ViewportRevision));
            return;
        }

        var replacement = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        replacement.SetSceneRasterMode(
            isWireframe_
                ? ViewportSceneRasterMode.Wireframe
                : ViewportSceneRasterMode.Solid);
        ReplaceSession(replacement);
        viewportRevision_ = document.Revision;
        OnPropertyChanged(nameof(ViewportRevision));
    }

    private void ReplaceSession(ViewportSession? session)
    {
        if (ReferenceEquals(session_, session))
        {
            return;
        }

        session_?.Close();
        session_ = session;
        if (session is null)
        {
            viewportRevision_ = 0;
        }
        NotifyViewportChanged();
    }

    private void NotifyViewportChanged()
    {
        OnPropertyChanged(nameof(Session));
        OnPropertyChanged(nameof(ViewportRevision));
    }

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

internal sealed class StudioInspectorPanelViewModel(StudioShellViewModel shell)
    : StudioDockPanelViewModel(shell);

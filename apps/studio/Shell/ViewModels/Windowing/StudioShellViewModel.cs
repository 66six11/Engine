using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Threading;
using Editor.Shell.Commands;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Services.Projects;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Panels;
using Editor.UI.Icons;

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
    private readonly AsyncCommand closeProjectCommand_;
    private readonly AsyncCommand createEntityCommand_;
    private readonly AsyncCommand saveSceneCommand_;
    private readonly AsyncCommand applyEntityNameCommand_;
    private readonly AsyncCommand applyEntityTransformCommand_;
    private readonly EditorDockWorkspaceViewModel dockWorkspace_;
    private readonly ViewportPresentationLifetime viewportPresentationLifetime_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private StudioShellStage stage_ = StudioShellStage.Starting;
    private ProjectSessionSnapshot projectSnapshot_;
    private SceneEntitySnapshot? selectedEntity_;
    private string newProjectName_ = "MyProject";
    private string projectOperationMessage_ = string.Empty;
    private string inspectorName_ = string.Empty;
    private string positionX_ = "0";
    private string positionY_ = "0";
    private string positionZ_ = "0";
    private string rotationX_ = "0";
    private string rotationY_ = "0";
    private string rotationZ_ = "0";
    private string rotationW_ = "1";
    private string scaleX_ = "1";
    private string scaleY_ = "1";
    private string scaleZ_ = "1";
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
        viewportPresentationLifetime_ = new ViewportPresentationLifetime();
        projectSnapshot_ = projectSession.Current;
        createProjectCommand_ = new AsyncCommand(CreateProjectAsync, CanCreateProject);
        openProjectCommand_ = new AsyncCommand(OpenProjectAsync, CanRunProjectOperation);
        closeProjectCommand_ = new AsyncCommand(CloseProjectAsync, CanEditDocument);
        createEntityCommand_ = new AsyncCommand(CreateEntityAsync, CanEditDocument);
        saveSceneCommand_ = new AsyncCommand(SaveSceneAsync, CanSaveDocument);
        applyEntityNameCommand_ = new AsyncCommand(ApplyEntityNameAsync, CanEditSelection);
        applyEntityTransformCommand_ = new AsyncCommand(
            ApplyEntityTransformAsync,
            CanEditSelection);
        dockWorkspace_ = CreateDockWorkspace();
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
            var document = projectSnapshot_.Document;
            if (document is null)
            {
                return $"No Document - {project} - Asharia Studio";
            }
            var dirty = document.IsDirty ? "*" : string.Empty;
            return $"{dirty}{Path.GetFileName(document.Path)} - {project} - Asharia Studio";
        }
    }

    public bool IsStarting => stage_ == StudioShellStage.Starting;

    public bool IsWorkspaceVisible => stage_ == StudioShellStage.Ready;

    public bool HasProject => projectSnapshot_.IsReady;

    public bool HasNoProject => !projectSnapshot_.IsReady;

    public bool HasDocument => projectSnapshot_.Document is not null;

    public bool HasNoDocument => projectSnapshot_.Document is null;

    public bool IsDocumentDirty => projectSnapshot_.Document?.IsDirty == true;

    public bool HasSelection => SelectedEntity is not null;

    public string StartingStateText => "Starting";

    public string ProjectStateText =>
        projectSnapshot_.Project?.ProjectName ?? "No Project";

    public string ProjectPathText =>
        projectSnapshot_.Project?.RootPath ?? string.Empty;

    public string DocumentStateText => projectSnapshot_.Document is { } document
        ? $"{Path.GetFileName(document.Path)} · revision {document.Revision} · " +
          (document.IsDirty ? "Unsaved" : "Saved")
        : "No Document";

    public string DocumentPathText => projectSnapshot_.Document?.Path ?? string.Empty;

    public IReadOnlyList<SceneEntitySnapshot> SceneEntities =>
        projectSnapshot_.Document?.Entities ?? [];

    public SceneEntitySnapshot? SelectedEntity
    {
        get => selectedEntity_;
        set
        {
            if (Equals(selectedEntity_, value))
            {
                return;
            }
            selectedEntity_ = value;
            LoadInspector(value);
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasSelection));
            RaiseProjectCommandStateChanged();
        }
    }

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

    public string InspectorName
    {
        get => inspectorName_;
        set => SetField(ref inspectorName_, value);
    }

    public string PositionX { get => positionX_; set => SetField(ref positionX_, value); }
    public string PositionY { get => positionY_; set => SetField(ref positionY_, value); }
    public string PositionZ { get => positionZ_; set => SetField(ref positionZ_, value); }
    public string RotationX { get => rotationX_; set => SetField(ref rotationX_, value); }
    public string RotationY { get => rotationY_; set => SetField(ref rotationY_, value); }
    public string RotationZ { get => rotationZ_; set => SetField(ref rotationZ_, value); }
    public string RotationW { get => rotationW_; set => SetField(ref rotationW_, value); }
    public string ScaleX { get => scaleX_; set => SetField(ref scaleX_, value); }
    public string ScaleY { get => scaleY_; set => SetField(ref scaleY_, value); }
    public string ScaleZ { get => scaleZ_; set => SetField(ref scaleZ_, value); }

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
    public ICommand CloseProjectCommand => closeProjectCommand_;
    public ICommand CreateEntityCommand => createEntityCommand_;
    public ICommand SaveSceneCommand => saveSceneCommand_;
    public ICommand ApplyEntityNameCommand => applyEntityNameCommand_;
    public ICommand ApplyEntityTransformCommand => applyEntityTransformCommand_;
    public EditorDockWorkspaceViewModel DockWorkspace => dockWorkspace_;

    internal IProjectSession ProjectSession => projectSession_;

    internal ViewportPresentationLifetime ViewportPresentationLifetime =>
        viewportPresentationLifetime_;

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
        dockWorkspace_.Dispose();
        lifetimeCancellation_.Cancel();
        RaiseProjectCommandStateChanged();
    }

    private EditorDockWorkspaceViewModel CreateDockWorkspace()
    {
        var panels = new PanelRegistry();
        panels.Register(new PanelDescriptor(
            "hierarchy",
            "Hierarchy",
            PanelKind.Tool,
            EditorDockArea.Left,
            "Window/Panels/Hierarchy",
            DockContentCachePolicy.KeepAlive,
            () => new StudioHierarchyPanelViewModel(this),
            EditorIconKey.PanelHierarchy,
            "SCENE",
            "Scene entities",
            "tool"));
        panels.Register(new PanelDescriptor(
            "project",
            "Project",
            PanelKind.Tool,
            EditorDockArea.Left,
            "Window/Panels/Project",
            DockContentCachePolicy.KeepAlive,
            () => new StudioProjectPanelViewModel(this),
            EditorIconKey.PanelProject,
            "PROJECT",
            "Project content",
            "tool"));
        panels.Register(new PanelDescriptor(
            "scene-view",
            "Scene Document",
            PanelKind.Document,
            EditorDockArea.Center,
            "Window/Panels/Scene Document",
            DockContentCachePolicy.KeepAlive,
            () => new StudioScenePanelViewModel(this),
            EditorIconKey.PanelSceneView,
            "DOC",
            "Authoritative edit world",
            "document"));
        panels.Register(new PanelDescriptor(
            "inspector",
            "Inspector",
            PanelKind.Tool,
            EditorDockArea.Right,
            "Window/Panels/Inspector",
            DockContentCachePolicy.KeepAlive,
            () => new StudioInspectorPanelViewModel(this),
            EditorIconKey.PanelInspector,
            "ENTITY",
            "Selection context",
            "tool"));
        return new EditorDockWorkspaceViewModel(panels);
    }

    private bool CanCreateProject() =>
        CanRunProjectOperation() && !string.IsNullOrWhiteSpace(NewProjectName);

    private bool CanRunProjectOperation() =>
        !isDisposed_ && stage_ == StudioShellStage.Ready && !IsProjectOperationRunning;

    private bool CanEditDocument() => CanRunProjectOperation() && HasDocument;

    private bool CanSaveDocument() => CanEditDocument() && IsDocumentDirty;

    private bool CanEditSelection() => CanEditDocument() && HasSelection;

    private async Task CreateProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var parent = await projectDialogs_.SelectProjectParentDirectoryAsync(token);
            return parent is null
                ? null
                : await ExecuteWithPresentationDrainAsync(
                    () => projectSession_.CreateProjectAsync(parent, NewProjectName, token));
        });
    }

    private async Task OpenProjectAsync()
    {
        await RunProjectOperationAsync(async token =>
        {
            var descriptor = await projectDialogs_.SelectProjectDescriptorAsync(token);
            return descriptor is null
                ? null
                : await ExecuteWithPresentationDrainAsync(
                    () => projectSession_.OpenProjectAsync(descriptor, token));
        });
    }

    private Task CloseProjectAsync() =>
        RunProjectOperationAsync(async token =>
            await ExecuteWithPresentationDrainAsync(
                () => projectSession_.CloseProjectAsync(token)));

    private async Task CreateEntityAsync()
    {
        await RunProjectOperationAsync(async token =>
            await projectSession_.CreateEntityAsync("Entity", token));
        SelectedEntity = SceneEntities.LastOrDefault();
    }

    private Task SaveSceneAsync() =>
        RunProjectOperationAsync(async token =>
            await projectSession_.SaveSceneAsync(token));

    private Task ApplyEntityNameAsync()
    {
        var selected = SelectedEntity;
        return selected is null
            ? Task.CompletedTask
            : RunProjectOperationAsync(async token =>
                await projectSession_.SetEntityNameAsync(
                    selected.ObjectId,
                    InspectorName,
                    token));
    }

    private Task ApplyEntityTransformAsync()
    {
        var selected = SelectedEntity;
        if (selected is null)
        {
            return Task.CompletedTask;
        }
        if (!TryReadTransform(out var transform))
        {
            ProjectOperationMessage =
                "Transform fields must be finite invariant-culture numbers with a unit quaternion.";
            return Task.CompletedTask;
        }
        return RunProjectOperationAsync(async token =>
            await projectSession_.SetEntityTransformAsync(
                selected.ObjectId,
                transform,
                token));
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

    private async ValueTask<ProjectSessionOperationResult>
        ExecuteWithPresentationDrainAsync(
            Func<ValueTask<ProjectSessionOperationResult>> operation)
    {
        await using var pause =
            await viewportPresentationLifetime_.PauseAndDrainAsync();
        return await operation();
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
        var selectedObjectId = selectedEntity_?.ObjectId;
        projectSnapshot_ = snapshot;
        var nextSelection = selectedObjectId is { } id
            ? snapshot.Document?.Entities.FirstOrDefault(entity => entity.ObjectId == id)
            : null;
        if (!Equals(selectedEntity_, nextSelection))
        {
            selectedEntity_ = nextSelection;
            LoadInspector(nextSelection);
            OnPropertyChanged(nameof(SelectedEntity));
            OnPropertyChanged(nameof(HasSelection));
        }
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(HasProject));
        OnPropertyChanged(nameof(HasNoProject));
        OnPropertyChanged(nameof(HasDocument));
        OnPropertyChanged(nameof(HasNoDocument));
        OnPropertyChanged(nameof(IsDocumentDirty));
        OnPropertyChanged(nameof(ProjectStateText));
        OnPropertyChanged(nameof(ProjectPathText));
        OnPropertyChanged(nameof(DocumentStateText));
        OnPropertyChanged(nameof(DocumentPathText));
        OnPropertyChanged(nameof(SceneEntities));
        RaiseProjectCommandStateChanged();
    }

    private void LoadInspector(SceneEntitySnapshot? entity)
    {
        var transform = entity?.Transform ?? TransformValue.Identity;
        inspectorName_ = entity?.Name ?? string.Empty;
        positionX_ = Format(transform.Position.X);
        positionY_ = Format(transform.Position.Y);
        positionZ_ = Format(transform.Position.Z);
        rotationX_ = Format(transform.Rotation.X);
        rotationY_ = Format(transform.Rotation.Y);
        rotationZ_ = Format(transform.Rotation.Z);
        rotationW_ = Format(transform.Rotation.W);
        scaleX_ = Format(transform.Scale.X);
        scaleY_ = Format(transform.Scale.Y);
        scaleZ_ = Format(transform.Scale.Z);
        OnPropertyChanged(nameof(InspectorName));
        OnPropertyChanged(nameof(PositionX));
        OnPropertyChanged(nameof(PositionY));
        OnPropertyChanged(nameof(PositionZ));
        OnPropertyChanged(nameof(RotationX));
        OnPropertyChanged(nameof(RotationY));
        OnPropertyChanged(nameof(RotationZ));
        OnPropertyChanged(nameof(RotationW));
        OnPropertyChanged(nameof(ScaleX));
        OnPropertyChanged(nameof(ScaleY));
        OnPropertyChanged(nameof(ScaleZ));
    }

    private bool TryReadTransform(out TransformValue transform)
    {
        transform = default;
        if (!TryParse(PositionX, out var px) || !TryParse(PositionY, out var py) ||
            !TryParse(PositionZ, out var pz) || !TryParse(RotationX, out var rx) ||
            !TryParse(RotationY, out var ry) || !TryParse(RotationZ, out var rz) ||
            !TryParse(RotationW, out var rw) || !TryParse(ScaleX, out var sx) ||
            !TryParse(ScaleY, out var sy) || !TryParse(ScaleZ, out var sz))
        {
            return false;
        }
        var lengthSquared = rx * rx + ry * ry + rz * rz + rw * rw;
        if (Math.Abs(lengthSquared - 1.0f) > 1.0e-3f)
        {
            return false;
        }
        transform = new TransformValue(
            new Float3(px, py, pz),
            new Quaternion(rx, ry, rz, rw),
            new Float3(sx, sy, sz));
        return true;
    }

    private static bool TryParse(string text, out float value) =>
        float.TryParse(
            text,
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out value) && float.IsFinite(value);

    private static string Format(float value) =>
        value.ToString("G9", CultureInfo.InvariantCulture);

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
        closeProjectCommand_.RaiseCanExecuteChanged();
        createEntityCommand_.RaiseCanExecuteChanged();
        saveSceneCommand_.RaiseCanExecuteChanged();
        applyEntityNameCommand_.RaiseCanExecuteChanged();
        applyEntityTransformCommand_.RaiseCanExecuteChanged();
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(isDisposed_, this);

    private void SetField(
        ref string field,
        string value,
        [CallerMemberName] string? propertyName = null)
    {
        if (string.Equals(field, value, StringComparison.Ordinal))
        {
            return;
        }
        field = value;
        OnPropertyChanged(propertyName);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using System.IO;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Projects;
using Asharia.Editor.Selection;
using Asharia.Editor.Tasks;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Studio.Application.Commands;
using Avalonia;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models.Workbench;
using Editor.Core.Services;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Asharia.Studio.Application.Lifecycle;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Tasks;
using Editor.Shell.Services;
using Editor.Shell.Lifecycle;
using Editor.Shell.ViewModels.CommandPalette;
using Editor.Shell.ViewModels.Dialogs;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Menus;
using Editor.Shell.ViewModels.Projects;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Windowing;

public class MainWindowViewModel : ViewModelBase, IDisposable
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly PanelCommandService panelCommandService_;
    private readonly WorkbenchShortcutRouter shortcutRouter_;
    private readonly IEditorBackgroundTaskService backgroundTasks_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private readonly IEditorDiagnosticService diagnostics_;
    private readonly IProjectSessionService projectSessions_;
    private readonly RelayCommand openStatusMessageTargetCommand_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;
    private bool hasActiveBackgroundTasks_;
    private string activeBackgroundTaskTitle_ = string.Empty;
    private string activeBackgroundTaskMessage_ = string.Empty;
    private string selectionSummary_ = "Nothing selected";
    private string diagnosticSummary_ = "No diagnostics";
    private ProjectSessionSnapshot projectSession_;
    private EditorStatusMessageSnapshot? lastStatusMessage_;
    private EditorDiagnosticRecord? latestStatusDiagnostic_;
    private EditorStatusMessageSeverity? statusMessageSeverity_;
    private bool isDisposed_;

    public MainWindowViewModel()
        : this(EditorDockLayoutStore.TryLoad())
    {
    }

    private MainWindowViewModel(EditorDockLayoutSnapshot? savedLayout)
        : this(CreateDefaultViewModelArguments(savedLayout))
    {
    }

    private MainWindowViewModel(MainWindowViewModelArguments arguments)
        : this(
            arguments.Composition.PanelRegistry,
            arguments.Composition.ActionRegistry,
            arguments.SavedLayout,
            arguments.SelectionService,
            diagnostics: arguments.Diagnostics,
            projectOpenSessions: arguments.ProjectOpenSessions,
            projectSessions: arguments.ProjectSessions,
            defaultLayoutFactory: arguments.DefaultLayoutFactory)
    {
    }

    internal MainWindowViewModel(
        IPanelRegistry panelRegistry,
        IWorkbenchActionRegistry actionRegistry,
        EditorDockLayoutSnapshot? savedLayout,
        IEditorSelectionService? selectionService = null,
        IEditorBackgroundTaskService? backgroundTasks = null,
        IEditorUiDispatcher? uiDispatcher = null,
        IEditorLifecycleEventService? lifecycleEvents = null,
        IEditorDiagnosticService? diagnostics = null,
        IProjectOpenSessionSnapshotSource? projectOpenSessions = null,
        IProjectSessionService? projectSessions = null,
        Func<EditorDockLayoutSnapshot>? defaultLayoutFactory = null)
    {
        SelectionService = selectionService ?? new EditorSelectionService();
        SelectionService.SelectionChanged += OnSelectionChanged;
        panelRegistry_ = panelRegistry;
        backgroundTasks_ = backgroundTasks ?? new EditorBackgroundTaskService();
        uiDispatcher_ = uiDispatcher ?? new AvaloniaEditorUiDispatcher();
        diagnostics_ = diagnostics ?? new EditorDiagnosticService();
        diagnostics_.DiagnosticsChanged += OnDiagnosticsChanged;
        projectSessions_ = projectSessions ?? UnavailableProjectSessionService.Instance;
        projectSession_ = projectSessions_.Current;
        projectSessions_.SnapshotChanged += OnProjectSessionChanged;
        ProjectLaunch = new ProjectLaunchViewModel(
            projectOpenSessions ?? new ProjectOpenSessionSnapshotSource(),
            uiDispatcher_);
        RefreshLatestDiagnostic();
        backgroundTasks_.TasksChanged += OnBackgroundTasksChanged;
        RefreshBackgroundTaskSummary();

        LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
        DockWorkspace = new EditorDockWorkspaceViewModel(
            panelRegistry_,
            LifecycleEvents,
            panelFrameScheduler: null,
            defaultLayoutFactory: defaultLayoutFactory,
            initiallyFocused: false);
        panelCommandService_ = new PanelCommandService(DockWorkspace);
        DialogHost = new EditorDialogHostViewModel();
        var actions = actionRegistry.GetAll();
        var commandHandlers = WorkbenchCommandHandlerRegistry.CreateBuiltIn(
            actions,
            panelCommandService_,
            OpenCommandPaletteFromCommand,
            OpenAboutDialogFromCommand);
        var actionExecutor = new WorkbenchActionExecutor(commandHandlers);
        var commandRouter = new EditorCommandStatusMessageRouter(
            new WorkbenchCommandRouter(actionRegistry, actionExecutor),
            PublishCommandStatusMessage);
        panelCommandService_.PanelStateChanged += OnPanelCommandStateChanged;
        OpenPanelCommand = new RelayCommand<string?>(
            panelId => panelCommandService_.OpenOrFocusPanel(panelId));
        openStatusMessageTargetCommand_ = new RelayCommand(
            OpenStatusMessageTarget,
            () => CanOpenStatusMessageTarget);
        CommandPalette = new CommandPaletteViewModel(actions, commandRouter.Execute);
        shortcutRouter_ = WorkbenchShortcutRouter.FromActions(actions, commandRouter);
        ToolsMenuItems = CreateCommandMenuItems(actions, "Tools/", commandRouter);
        HelpMenuItems = CreateCommandMenuItems(actions, "Help/", commandRouter);
        PanelMenuItems = CreatePanelMenuItems(actions, commandRouter);
        DockWorkspace.RestoreLayoutSnapshot(savedLayout);
        if (savedLayout?.FloatingWindows is { Count: > 0 } floatingWindows)
        {
            pendingFloatingWindowSnapshots_.AddRange(floatingWindows);
        }
        RefreshPanelMenuOpenStates();
        RefreshSelectionSummary();

        SaveLayoutCommand = new RelayCommand(SaveLayout);
        ResetLayoutCommand = new RelayCommand(ResetLayout);
        ApplyCompactLayoutCommand = new RelayCommand(ApplyCompactLayout);
    }

    public IEditorSelectionService SelectionService { get; }

    public IEditorLifecycleEventService LifecycleEvents { get; }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IRelayCommand SaveLayoutCommand { get; }

    public IRelayCommand ResetLayoutCommand { get; }

    public IRelayCommand ApplyCompactLayoutCommand { get; }

    public IRelayCommand<string?> OpenPanelCommand { get; }

    public IRelayCommand OpenStatusMessageTargetCommand => openStatusMessageTargetCommand_;

    public CommandPaletteViewModel CommandPalette { get; }

    public EditorDialogHostViewModel DialogHost { get; }

    public ProjectLaunchViewModel ProjectLaunch { get; }

    public IReadOnlyList<WorkbenchMenuItemViewModel> ToolsMenuItems { get; }

    public IReadOnlyList<WorkbenchMenuItemViewModel> HelpMenuItems { get; }

    public IReadOnlyList<PanelMenuItemViewModel> PanelMenuItems { get; }

    public string ActiveProjectDisplayName =>
        projectSession_.Project?.ProjectName ?? "No active project";

    public string DocumentDisplayName =>
        HasActiveProject ? "Untitled Scene" : "No document";

    public bool HasActiveProject => projectSession_.IsReady;

    public bool IsDocumentDirty => false;

    public string WindowTitle =>
        $"{DocumentDisplayName} — {ActiveProjectDisplayName} — Asharia Studio";

    public string EditorModeText => "Edit";

    public string ToolUnavailableReason =>
        "Viewport tools are unavailable until a tool service is connected.";

    public string SessionUnavailableReason => HasActiveProject
        ? "Run controls are unavailable until a runtime session is connected."
        : "Run controls are unavailable until a project is active.";

    public string SelectionSummary
    {
        get => selectionSummary_;
        private set => SetProperty(ref selectionSummary_, value);
    }

    public string BackgroundTaskSummary =>
        HasActiveBackgroundTasks ? ActiveBackgroundTaskTitle : "No active tasks";

    public string DiagnosticSummary
    {
        get => diagnosticSummary_;
        private set => SetProperty(ref diagnosticSummary_, value);
    }

    public bool HasActiveBackgroundTasks
    {
        get => hasActiveBackgroundTasks_;
        private set => SetProperty(ref hasActiveBackgroundTasks_, value);
    }

    public string ActiveBackgroundTaskTitle
    {
        get => activeBackgroundTaskTitle_;
        private set => SetProperty(ref activeBackgroundTaskTitle_, value);
    }

    public string ActiveBackgroundTaskMessage
    {
        get => activeBackgroundTaskMessage_;
        private set => SetProperty(ref activeBackgroundTaskMessage_, value);
    }

    public EditorStatusMessageSnapshot? LastStatusMessage
    {
        get => lastStatusMessage_;
        private set
        {
            if (SetProperty(ref lastStatusMessage_, value))
            {
                OnStatusMessageProjectionChanged();
            }
        }
    }

    public bool HasStatusMessage => latestStatusDiagnostic_ is not null || LastStatusMessage is not null;

    public string StatusMessageText =>
        latestStatusDiagnostic_?.Message ?? LastStatusMessage?.Message ?? string.Empty;

    public bool IsStatusMessageDebug =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Debug;

    public bool IsStatusMessageInfo =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Info;

    public bool IsStatusMessageSuccess =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Success;

    public bool IsStatusMessageWarning =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Warning;

    public bool IsStatusMessageError =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Error;

    public bool CanOpenStatusMessageTarget =>
        panelCommandService_.CanOpenOrFocusPanel(LastStatusMessage?.TargetPanelId);

    private EditorStatusMessageSeverity? CurrentStatusMessageSeverity =>
        statusMessageSeverity_ ?? LastStatusMessage?.Severity;

    public void SetFloatingWindowCallbacks(
        Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>> captureFloatingWindowSnapshots,
        Action closeFloatingWindows,
        Func<string, bool> activateFloatingPanel,
        Func<string, bool> isFloatingPanelOpen,
        Func<string, bool>? closeFloatingPanel = null)
    {
        captureFloatingWindowSnapshots_ = captureFloatingWindowSnapshots;
        closeFloatingWindows_ = closeFloatingWindows;
        panelCommandService_.SetExternalPanelCallbacks(
            activateFloatingPanel,
            isFloatingPanelOpen,
            closeFloatingPanel);
    }

    public IReadOnlyList<EditorDockFloatingWindowRequest> ConsumeRestoredFloatingWindowRequests()
    {
        if (pendingFloatingWindowSnapshots_.Count == 0)
        {
            return [];
        }

        var requests = new List<EditorDockFloatingWindowRequest>();
        foreach (var snapshot in pendingFloatingWindowSnapshots_)
        {
            if (!DockWorkspace.TryCreateFloatingWorkspace(
                    snapshot,
                    out var floatingWorkspace))
            {
                continue;
            }

            var window = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
            var bounds = new Rect(
                snapshot.X,
                snapshot.Y,
                Math.Max(240, snapshot.Width),
                Math.Max(180, snapshot.Height));
            requests.Add(new EditorDockFloatingWindowRequest(window, bounds));
        }

        pendingFloatingWindowSnapshots_.Clear();
        return requests;
    }

    public ProjectSessionOperationResult CreateMinimalProject(string projectRoot)
    {
        var result = projectSessions_.CreateMinimalProject(
            projectRoot,
            ProjectNameFromRoot(projectRoot));
        PublishProjectOperationResult("project.create-minimal", result);
        return result;
    }

    public ProjectSessionOperationResult OpenProject(string projectRoot)
    {
        var result = projectSessions_.OpenProject(projectRoot);
        PublishProjectOperationResult("project.open", result);
        return result;
    }

    public void RefreshPanelMenuOpenStates()
    {
        foreach (var item in PanelMenuItems)
        {
            item.SetOpenState(panelCommandService_.IsPanelOpen(item.PanelId));
        }
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        var exceptions = new CallbackExceptionBatch();
        exceptions.Capture(() => closeFloatingWindows_?.Invoke());
        exceptions.Capture(
            () => SelectionService.SelectionChanged -= OnSelectionChanged);
        exceptions.Capture(
            () => backgroundTasks_.TasksChanged -= OnBackgroundTasksChanged);
        exceptions.Capture(
            () => diagnostics_.DiagnosticsChanged -= OnDiagnosticsChanged);
        exceptions.Capture(
            () => projectSessions_.SnapshotChanged -= OnProjectSessionChanged);
        exceptions.Capture(ProjectLaunch.Dispose);
        exceptions.Capture(
            () => panelCommandService_.PanelStateChanged -= OnPanelCommandStateChanged);
        exceptions.Capture(DockWorkspace.Dispose);
        exceptions.ThrowIfAny();
    }

    internal EditorCommandExecutionResult? ExecuteShortcut(
        Key key,
        KeyModifiers keyModifiers,
        bool isTextInputFocused)
    {
        return shortcutRouter_.TryExecute(key, keyModifiers, isTextInputFocused);
    }

    private void SaveLayout()
    {
        var snapshot = DockWorkspace.CaptureLayoutSnapshot();
        if (captureFloatingWindowSnapshots_ is not null)
        {
            snapshot.FloatingWindows.AddRange(captureFloatingWindowSnapshots_());
        }

        EditorDockLayoutStore.TrySave(snapshot);
    }

    private void ResetLayout()
    {
        pendingFloatingWindowSnapshots_.Clear();
        closeFloatingWindows_?.Invoke();
        DockWorkspace.ResetLayout();
        EditorDockLayoutStore.TryDelete();
    }

    private void ApplyCompactLayout()
    {
        pendingFloatingWindowSnapshots_.Clear();
        closeFloatingWindows_?.Invoke();
        _ = DockWorkspace.RestoreLayoutSnapshot(
            EditorWorkbenchLayoutPreset.CreateCompact());
        EditorDockLayoutStore.TryDelete();
    }

    private bool OpenCommandPaletteFromCommand()
    {
        if (!CommandPalette.OpenCommand.CanExecute(null))
        {
            return false;
        }

        CommandPalette.OpenCommand.Execute(null);
        return true;
    }

    private bool OpenAboutDialogFromCommand()
    {
        _ = DialogHost.ShowAsync(StudioDialogRequests.About());
        return true;
    }

    private void OnPanelCommandStateChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
        OnPropertyChanged(nameof(CanOpenStatusMessageTarget));
        openStatusMessageTargetCommand_.NotifyCanExecuteChanged();
    }

    private void OnBackgroundTasksChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshBackgroundTaskSummary();
            return;
        }

        uiDispatcher_.Post(RefreshBackgroundTaskSummary);
    }

    private void OnSelectionChanged(
        object? sender,
        EditorSelectionChangedEventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshSelectionSummary();
            return;
        }

        uiDispatcher_.Post(RefreshSelectionSummary);
    }

    private void OnDiagnosticsChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshLatestDiagnostic();
            return;
        }

        uiDispatcher_.Post(RefreshLatestDiagnostic);
    }

    private void OnProjectSessionChanged(object? sender, EventArgs e)
    {
        if (isDisposed_)
        {
            return;
        }
        if (uiDispatcher_.CheckAccess())
        {
            RefreshProjectSessionProjection();
            return;
        }

        uiDispatcher_.Post(RefreshProjectSessionProjection);
    }

    private void RefreshBackgroundTaskSummary()
    {
        var activeBackgroundTasks = backgroundTasks_.GetActiveSnapshots();
        if (activeBackgroundTasks.Count == 0)
        {
            HasActiveBackgroundTasks = false;
            ActiveBackgroundTaskTitle = string.Empty;
            ActiveBackgroundTaskMessage = string.Empty;
            OnPropertyChanged(nameof(BackgroundTaskSummary));
            return;
        }

        var activeBackgroundTask = activeBackgroundTasks[0];
        HasActiveBackgroundTasks = true;
        ActiveBackgroundTaskTitle = activeBackgroundTask.Title;
        ActiveBackgroundTaskMessage = activeBackgroundTask.Message ?? string.Empty;
        OnPropertyChanged(nameof(BackgroundTaskSummary));
    }

    private void RefreshSelectionSummary()
    {
        var selection = SelectionService.Current;
        SelectionSummary = selection.Items.Count switch
        {
            0 => "Nothing selected",
            1 => selection.Items[0].DisplayName,
            _ => $"{selection.Items.Count} items selected",
        };
    }

    private void RefreshProjectSessionProjection()
    {
        if (isDisposed_)
        {
            return;
        }

        projectSession_ = projectSessions_.Current;
        OnPropertyChanged(nameof(ActiveProjectDisplayName));
        OnPropertyChanged(nameof(DocumentDisplayName));
        OnPropertyChanged(nameof(HasActiveProject));
        OnPropertyChanged(nameof(WindowTitle));
        OnPropertyChanged(nameof(SessionUnavailableReason));
    }

    private void PublishProjectOperationResult(
        string commandId,
        ProjectSessionOperationResult result)
    {
        PublishCommandStatusMessage(new EditorCommandExecutionResult(
            result.Succeeded
                ? EditorCommandExecutionStatus.Succeeded
                : EditorCommandExecutionStatus.Failed,
            commandId,
            result.Message));
    }

    private static string ProjectNameFromRoot(string projectRoot)
    {
        if (string.IsNullOrWhiteSpace(projectRoot))
        {
            return "Untitled Project";
        }

        var trimmedRoot = projectRoot.TrimEnd(
            Path.DirectorySeparatorChar,
            Path.AltDirectorySeparatorChar);
        var name = Path.GetFileName(trimmedRoot);
        return string.IsNullOrWhiteSpace(name)
            ? "Untitled Project"
            : name;
    }

    internal void PublishStatusMessage(EditorStatusMessageSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        LastStatusMessage = snapshot;
    }

    private void PublishCommandStatusMessage(EditorCommandExecutionResult result)
    {
        var statusMessage = EditorStatusMessageSnapshot.FromCommandResult(result);
        LastStatusMessage = statusMessage;
        diagnostics_.Publish(
            MapStatusMessageSeverity(statusMessage.Severity),
            EditorDiagnosticChannel.Debug,
            result.CommandId,
            "workbench",
            statusMessage.Message);
    }

    private void OpenStatusMessageTarget()
    {
        var targetPanelId = LastStatusMessage?.TargetPanelId;
        if (!panelCommandService_.CanOpenOrFocusPanel(targetPanelId))
        {
            return;
        }

        panelCommandService_.OpenOrFocusPanel(targetPanelId);
    }

    private void RefreshLatestDiagnostic()
    {
        var latestDiagnostic = diagnostics_.GetLatestDiagnostic();
        if (latestStatusDiagnostic_ == latestDiagnostic)
        {
            return;
        }

        latestStatusDiagnostic_ = latestDiagnostic;
        statusMessageSeverity_ = ResolveStatusMessageSeverity(latestDiagnostic);
        RefreshDiagnosticSummary();
        OnStatusMessageProjectionChanged();
    }

    private void RefreshDiagnosticSummary()
    {
        var warningCount = 0;
        var errorCount = 0;
        foreach (var diagnostic in diagnostics_.GetRecentDiagnostics())
        {
            if (diagnostic.Severity == EditorDiagnosticSeverity.Warning)
            {
                warningCount++;
            }
            else if (diagnostic.Severity == EditorDiagnosticSeverity.Error)
            {
                errorCount++;
            }
        }

        DiagnosticSummary = (errorCount, warningCount) switch
        {
            ( > 0, > 0) =>
                $"{FormatDiagnosticCount(errorCount, "error")}, {FormatDiagnosticCount(warningCount, "warning")}",
            ( > 0, _) => FormatDiagnosticCount(errorCount, "error"),
            (_, > 0) => FormatDiagnosticCount(warningCount, "warning"),
            _ => "No diagnostics",
        };
    }

    private static string FormatDiagnosticCount(int count, string label) =>
        $"{count} {label}{(count == 1 ? string.Empty : "s")}";

    private void OnStatusMessageProjectionChanged()
    {
        OnPropertyChanged(nameof(HasStatusMessage));
        OnPropertyChanged(nameof(StatusMessageText));
        OnPropertyChanged(nameof(IsStatusMessageDebug));
        OnPropertyChanged(nameof(IsStatusMessageInfo));
        OnPropertyChanged(nameof(IsStatusMessageSuccess));
        OnPropertyChanged(nameof(IsStatusMessageWarning));
        OnPropertyChanged(nameof(IsStatusMessageError));
        OnPropertyChanged(nameof(CanOpenStatusMessageTarget));
        openStatusMessageTargetCommand_.NotifyCanExecuteChanged();
    }

    private EditorStatusMessageSeverity? ResolveStatusMessageSeverity(
        EditorDiagnosticRecord? diagnostic)
    {
        if (diagnostic is null)
        {
            return LastStatusMessage?.Severity;
        }

        if (LastStatusMessage is { } statusMessage
            && string.Equals(statusMessage.Message, diagnostic.Message, StringComparison.Ordinal))
        {
            return statusMessage.Severity;
        }

        return diagnostic.Severity switch
        {
            EditorDiagnosticSeverity.Debug => EditorStatusMessageSeverity.Debug,
            EditorDiagnosticSeverity.Warning => EditorStatusMessageSeverity.Warning,
            EditorDiagnosticSeverity.Error => EditorStatusMessageSeverity.Error,
            _ => EditorStatusMessageSeverity.Info,
        };
    }

    private static EditorDiagnosticSeverity MapStatusMessageSeverity(EditorStatusMessageSeverity severity)
    {
        return severity switch
        {
            EditorStatusMessageSeverity.Debug => EditorDiagnosticSeverity.Debug,
            EditorStatusMessageSeverity.Warning => EditorDiagnosticSeverity.Warning,
            EditorStatusMessageSeverity.Error => EditorDiagnosticSeverity.Error,
            _ => EditorDiagnosticSeverity.Info,
        };
    }

    internal static IPanelRegistry CreatePanelRegistry(IEditorSelectionService? selectionService = null)
    {
        return CreateDefaultComposition(selectionService).PanelRegistry;
    }

    internal static IWorkbenchActionRegistry CreateWorkbenchActionRegistry(IEditorSelectionService? selectionService = null)
    {
        return CreateDefaultComposition(selectionService).ActionRegistry;
    }

    internal static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        return StudioCompositionRoot.CreateDefaultComposition(
            selectionService,
            diagnostics);
    }

    private static MainWindowViewModelArguments CreateDefaultViewModelArguments(
        EditorDockLayoutSnapshot? savedLayout)
    {
        var selectionService = new EditorSelectionService();
        var diagnostics = new EditorDiagnosticService();
        var projectOpenSessions = new ProjectOpenSessionSnapshotSource();
        return new MainWindowViewModelArguments(
            StudioCompositionRoot.CreateDefaultComposition(
                selectionService,
                diagnostics),
            savedLayout,
            selectionService,
            diagnostics,
            projectOpenSessions,
            UnavailableProjectSessionService.Instance,
            EditorWorkbenchLayoutPreset.CreateDefault);
    }

    private sealed record MainWindowViewModelArguments(
        EditorExtensionComposition Composition,
        EditorDockLayoutSnapshot? SavedLayout,
        IEditorSelectionService SelectionService,
        IEditorDiagnosticService Diagnostics,
        IProjectOpenSessionSnapshotSource ProjectOpenSessions,
        IProjectSessionService ProjectSessions,
        Func<EditorDockLayoutSnapshot> DefaultLayoutFactory);

    private sealed class UnavailableProjectSessionService : IProjectSessionService
    {
        public static UnavailableProjectSessionService Instance { get; } = new();

        public event EventHandler? SnapshotChanged
        {
            add { }
            remove { }
        }

        public ProjectSessionSnapshot Current => ProjectSessionSnapshot.NoProject;

        public ProjectSessionOperationResult CreateMinimalProject(
            string projectRoot,
            string projectName)
        {
            return ProjectSessionOperationResult.Failure(
                "Project creation is unavailable.");
        }

        public ProjectSessionOperationResult OpenProject(string projectRoot)
        {
            return ProjectSessionOperationResult.Failure(
                "Project opening is unavailable.");
        }
    }

    private IReadOnlyList<PanelMenuItemViewModel> CreatePanelMenuItems(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        IEditorGuiCommandExecutor commandRouter)
    {
        var items = new List<PanelMenuItemViewModel>();
        foreach (var action in actions)
        {
            if (action.Kind != WorkbenchActionKind.OpenPanel
                || !action.MenuPath.StartsWith("Window/Panels/", StringComparison.Ordinal))
            {
                continue;
            }

            items.Add(new PanelMenuItemViewModel(
                action,
                commandRouter.Execute));
        }

        return items;
    }

    private static IReadOnlyList<WorkbenchMenuItemViewModel> CreateCommandMenuItems(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        string menuPathPrefix,
        IEditorGuiCommandExecutor commandRouter)
    {
        var items = new List<WorkbenchMenuItemViewModel>();
        foreach (var action in actions)
        {
            if (!action.MenuPath.StartsWith(menuPathPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            items.Add(new WorkbenchMenuItemViewModel(
                action,
                commandRouter.Execute));
        }

        return items;
    }
}

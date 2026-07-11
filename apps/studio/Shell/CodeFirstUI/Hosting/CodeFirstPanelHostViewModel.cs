using System;
using Asharia.Editor.Commands;
using Editor.Core.Abstractions;
using Editor.Core.CodeFirstUI.Abstractions;
using Editor.Core.CodeFirstUI.Authoring;
using Editor.Core.CodeFirstUI.Building;
using Editor.Core.CodeFirstUI.Events;
using Editor.Core.CodeFirstUI.Models;
using Editor.Core.CodeFirstUI.State;
using Editor.Core.CodeFirstUI.Validation;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Workbench;
using Editor.Shell.CodeFirstUI.Adapters;
using Editor.UI.ViewModels;

namespace Editor.Shell.CodeFirstUI.Hosting;

internal sealed class CodeFirstPanelHostViewModel :
    ViewModelBase,
    IGuiAvaloniaHost,
    IEditorPanelLifecycleSink,
    IEditorPanelFrameUpdateSink,
    IDisposable
{
    private readonly IEditorGuiCommandExecutor commandExecutor_;
    private readonly GuiEventQueue events_ = new();
    private readonly CodeFirstEditorPanel panel_;
    private readonly GuiStateStore stateStore_ = new();
    private readonly IEditorUiDispatcher uiDispatcher_;
    private readonly GuiTreeValidator validator_ = new();
    private GuiTreeSnapshot? currentTree_;
    private bool isRebuildScheduled_;
    private bool isCreated_;
    private bool isDisposed_;
    private bool isEnabled_;
    private string? lastBuildErrorMessage_;
    private GuiTreeValidationResult lastValidationResult = GuiTreeValidationResult.Success;
    private EditorPanelLifecycleContext? lifecycleContext_;
    private GuiRebuildReason pendingRebuildReasons_;

    public CodeFirstPanelHostViewModel(
        CodeFirstEditorPanel panel,
        IEditorGuiCommandExecutor? commandExecutor = null,
        IEditorUiDispatcher? uiDispatcher = null)
    {
        ArgumentNullException.ThrowIfNull(panel);

        panel_ = panel;
        commandExecutor_ = commandExecutor ?? MissingCommandExecutor.Instance;
        uiDispatcher_ = uiDispatcher ?? ImmediateCodeFirstUiDispatcher.Instance;
    }

    public GuiTreeSnapshot? CurrentTree
    {
        get => currentTree_;
        private set => SetProperty(ref currentTree_, value);
    }

    public GuiTreeValidationResult LastValidationResult
    {
        get => lastValidationResult;
        private set
        {
            if (SetProperty(ref lastValidationResult, value))
            {
                OnPropertyChanged(nameof(HasValidationErrors));
            }
        }
    }

    public string? LastBuildErrorMessage
    {
        get => lastBuildErrorMessage_;
        private set
        {
            if (SetProperty(ref lastBuildErrorMessage_, value))
            {
                OnPropertyChanged(nameof(HasBuildError));
            }
        }
    }

    public bool HasBuildError => !string.IsNullOrWhiteSpace(lastBuildErrorMessage_);

    public bool HasValidationErrors => !lastValidationResult.IsValid;

    public EditorPanelFrameUpdateRequest FrameUpdateRequest => panel_.FrameUpdateRequest;

    public GuiEventQueue Events => events_;

    public GuiStateStore StateStore => stateStore_;

    public GuiRebuildReason LastRebuildReasons { get; private set; }

    internal void ClickButton(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        events_.EnqueueButtonClicked(nodeId);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void SelectListItem(GuiNodeId nodeId, string itemId)
    {
        SelectItem(nodeId, itemId);
    }

    internal void SelectComboBoxItem(GuiNodeId nodeId, string itemId)
    {
        SelectItem(nodeId, itemId);
    }

    internal void SelectRadioGroupItem(GuiNodeId nodeId, string itemId)
    {
        SelectItem(nodeId, itemId);
    }

    private void SelectItem(GuiNodeId nodeId, string itemId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(itemId))
        {
            throw new ArgumentException("Item id must not be empty.", nameof(itemId));
        }

        ThrowIfDisposed();
        if (stateStore_.TryGetSelectedItem(nodeId, out var currentItemId)
            && string.Equals(currentItemId, itemId, StringComparison.Ordinal))
        {
            return;
        }

        stateStore_.SetSelectedItem(nodeId, itemId);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void SelectNavigationRoute(GuiNodeId nodeId, string route)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(route))
        {
            throw new ArgumentException("Navigation route must not be empty.", nameof(route));
        }

        ThrowIfDisposed();
        if (stateStore_.TryGetSelectedRoute(nodeId, out var currentRoute)
            && string.Equals(currentRoute, route, StringComparison.Ordinal))
        {
            return;
        }

        stateStore_.SetSelectedRoute(nodeId, route);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void SetNavigationRouteExpanded(
        GuiNodeId nodeId,
        string route,
        bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(route))
        {
            throw new ArgumentException("Navigation route must not be empty.", nameof(route));
        }

        ThrowIfDisposed();
        if (stateStore_.TryGetNavigationRouteExpanded(nodeId, route, out var currentExpanded)
            && currentExpanded == isExpanded)
        {
            return;
        }

        stateStore_.SetNavigationRouteExpanded(nodeId, route, isExpanded);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void ResizeSplit(GuiNodeId nodeId, double ratio)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetSplitRatio(nodeId, ratio);
    }

    internal void SetSliderValue(GuiNodeId nodeId, double value)
    {
        SetNumericValue(nodeId, value);
    }

    internal void SetNumberInputValue(GuiNodeId nodeId, double value)
    {
        SetNumericValue(nodeId, value);
    }

    internal void SetColorValue(GuiNodeId nodeId, GuiColorValue value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetColorValue(nodeId, out var currentValue)
            && currentValue.Equals(value))
        {
            return;
        }

        stateStore_.SetColorValue(nodeId, value);
    }

    internal void SetVector3Value(GuiNodeId nodeId, GuiVector3Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetVector3Value(nodeId, out var currentValue)
            && currentValue.Equals(value))
        {
            return;
        }

        stateStore_.SetVector3Value(nodeId, value);
    }

    internal void SetVector2Value(GuiNodeId nodeId, GuiVector2Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetVector2Value(nodeId, out var currentValue)
            && currentValue.Equals(value))
        {
            return;
        }

        stateStore_.SetVector2Value(nodeId, value);
    }

    internal void SetVector4Value(GuiNodeId nodeId, GuiVector4Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetVector4Value(nodeId, out var currentValue)
            && currentValue.Equals(value))
        {
            return;
        }

        stateStore_.SetVector4Value(nodeId, value);
    }

    private void SetNumericValue(GuiNodeId nodeId, double value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetNumericValue(nodeId, out var currentValue)
            && currentValue.Equals(value))
        {
            return;
        }

        stateStore_.SetNumericValue(nodeId, value);
    }

    internal void SetText(GuiNodeId nodeId, string text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetText(nodeId, out var currentText)
            && string.Equals(currentText, text, StringComparison.Ordinal))
        {
            return;
        }

        stateStore_.SetText(nodeId, text);
    }

    internal void CommitText(GuiNodeId nodeId, string text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetText(nodeId, out var currentText)
            && string.Equals(currentText, text, StringComparison.Ordinal))
        {
            return;
        }

        stateStore_.SetText(nodeId, text);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void SetToggle(GuiNodeId nodeId, bool isChecked)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetToggle(nodeId, out var currentValue)
            && currentValue == isChecked)
        {
            return;
        }

        stateStore_.SetToggle(nodeId, isChecked);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    internal void SetFoldoutExpanded(GuiNodeId nodeId, bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        if (stateStore_.TryGetFoldoutExpanded(nodeId, out var currentValue)
            && currentValue == isExpanded)
        {
            return;
        }

        stateStore_.SetFoldoutExpanded(nodeId, isExpanded);
        RequestRebuild(GuiRebuildReason.InputEvent);
    }

    public void OnPanelAttached(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context;
        if (!isCreated_)
        {
            panel_.DispatchCreate(context);
            isCreated_ = true;
        }

        EnablePanel();
        RequestRebuild(GuiRebuildReason.InitialOpen);
    }

    public void OnPanelActivated(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context;
        RequestRebuild(GuiRebuildReason.LifecycleChanged);
    }

    public void OnPanelDeactivated(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context;
    }

    public void OnPanelDetached(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context;
        DisablePanel();
    }

    public void OnEditorPanelFrame(EditorPanelFrameContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context.Panel;
        panel_.DispatchFrame(context);
        if (context.IsRepaintRequested)
        {
            RequestRebuild(GuiRebuildReason.FrameTick);
        }
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        DisablePanel();
        panel_.DispatchDestroy();
        isDisposed_ = true;
    }

    internal void RequestRebuild(GuiRebuildReason reason)
    {
        ThrowIfDisposed();
        if (reason == GuiRebuildReason.None)
        {
            return;
        }

        pendingRebuildReasons_ |= reason;
        if (isRebuildScheduled_)
        {
            return;
        }

        isRebuildScheduled_ = true;
        uiDispatcher_.Post(FlushRebuild);
    }

    private void FlushRebuild()
    {
        if (isDisposed_)
        {
            return;
        }

        var reasons = pendingRebuildReasons_;
        pendingRebuildReasons_ = GuiRebuildReason.None;
        isRebuildScheduled_ = false;
        LastRebuildReasons = reasons;
        Rebuild();
    }

    private void Rebuild()
    {
        if (lifecycleContext_ is null)
        {
            return;
        }

        try
        {
            var builder = new GuiFrameBuilder(lifecycleContext_.PanelId);
            var gui = new EditorGui(builder, events_, stateStore_, commandExecutor_);
            panel_.DispatchGui(gui);
            var tree = builder.Build();
            var validation = validator_.Validate(tree);

            LastBuildErrorMessage = null;
            LastValidationResult = validation;
            if (validation.IsValid)
            {
                CurrentTree = tree;
            }
        }
        catch (Exception exception)
        {
            LastBuildErrorMessage = FormatBuildErrorMessage(exception);
            LastValidationResult = GuiTreeValidationResult.Success;
        }
    }

    private sealed class ImmediateCodeFirstUiDispatcher : IEditorUiDispatcher
    {
        public static ImmediateCodeFirstUiDispatcher Instance { get; } = new();

        public bool CheckAccess() => true;

        public void Post(Action action)
        {
            ArgumentNullException.ThrowIfNull(action);
            action();
        }
    }

    private static string FormatBuildErrorMessage(Exception exception)
    {
        return string.IsNullOrWhiteSpace(exception.Message)
            ? exception.GetType().Name
            : $"{exception.GetType().Name}: {exception.Message}";
    }

    private void EnablePanel()
    {
        if (isEnabled_)
        {
            return;
        }

        panel_.DispatchEnable();
        isEnabled_ = true;
    }

    private void DisablePanel()
    {
        if (!isEnabled_)
        {
            return;
        }

        panel_.DispatchDisable();
        isEnabled_ = false;
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(isDisposed_, this);
    }

    void IGuiAvaloniaHost.ClickButton(GuiNodeId nodeId)
    {
        ClickButton(nodeId);
    }

    void IGuiAvaloniaHost.SelectListItem(GuiNodeId nodeId, string itemId)
    {
        SelectListItem(nodeId, itemId);
    }

    void IGuiAvaloniaHost.SelectComboBoxItem(GuiNodeId nodeId, string itemId)
    {
        SelectComboBoxItem(nodeId, itemId);
    }

    void IGuiAvaloniaHost.SelectRadioGroupItem(GuiNodeId nodeId, string itemId)
    {
        SelectRadioGroupItem(nodeId, itemId);
    }

    void IGuiAvaloniaHost.SelectNavigationRoute(GuiNodeId nodeId, string route)
    {
        SelectNavigationRoute(nodeId, route);
    }

    void IGuiAvaloniaHost.SetNavigationRouteExpanded(
        GuiNodeId nodeId,
        string route,
        bool isExpanded)
    {
        SetNavigationRouteExpanded(nodeId, route, isExpanded);
    }

    void IGuiAvaloniaHost.ResizeSplit(GuiNodeId nodeId, double ratio)
    {
        ResizeSplit(nodeId, ratio);
    }

    void IGuiAvaloniaHost.SetSliderValue(GuiNodeId nodeId, double value)
    {
        SetSliderValue(nodeId, value);
    }

    void IGuiAvaloniaHost.SetNumberInputValue(GuiNodeId nodeId, double value)
    {
        SetNumberInputValue(nodeId, value);
    }

    void IGuiAvaloniaHost.SetColorValue(GuiNodeId nodeId, GuiColorValue value)
    {
        SetColorValue(nodeId, value);
    }

    void IGuiAvaloniaHost.SetVector3Value(GuiNodeId nodeId, GuiVector3Value value)
    {
        SetVector3Value(nodeId, value);
    }

    void IGuiAvaloniaHost.SetVector2Value(GuiNodeId nodeId, GuiVector2Value value)
    {
        SetVector2Value(nodeId, value);
    }

    void IGuiAvaloniaHost.SetVector4Value(GuiNodeId nodeId, GuiVector4Value value)
    {
        SetVector4Value(nodeId, value);
    }

    void IGuiAvaloniaHost.SetText(GuiNodeId nodeId, string text)
    {
        SetText(nodeId, text);
    }

    void IGuiAvaloniaHost.CommitText(GuiNodeId nodeId, string text)
    {
        CommitText(nodeId, text);
    }

    void IGuiAvaloniaHost.SetToggle(GuiNodeId nodeId, bool isChecked)
    {
        SetToggle(nodeId, isChecked);
    }

    void IGuiAvaloniaHost.SetFoldoutExpanded(GuiNodeId nodeId, bool isExpanded)
    {
        SetFoldoutExpanded(nodeId, isExpanded);
    }

    private sealed class MissingCommandExecutor : IEditorGuiCommandExecutor
    {
        public static MissingCommandExecutor Instance { get; } = new();

        public EditorCommandExecutionResult Execute(string commandId)
        {
            return EditorCommandExecutionResult.NotFound(commandId);
        }
    }
}

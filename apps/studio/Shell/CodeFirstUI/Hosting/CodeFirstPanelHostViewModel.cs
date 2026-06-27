using System;
using Editor.Core.Abstractions;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Editor.Shell.CodeFirstUI.Adapters;
using Editor.Shell.ViewModels;

namespace Editor.Shell.CodeFirstUI;

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
    private readonly GuiTreeValidator validator_ = new();
    private GuiTreeSnapshot? currentTree_;
    private bool isCreated_;
    private bool isDisposed_;
    private bool isEnabled_;
    private GuiTreeValidationResult lastValidationResult = GuiTreeValidationResult.Success;
    private EditorPanelLifecycleContext? lifecycleContext_;

    public CodeFirstPanelHostViewModel(
        CodeFirstEditorPanel panel,
        IEditorGuiCommandExecutor? commandExecutor = null)
    {
        ArgumentNullException.ThrowIfNull(panel);

        panel_ = panel;
        commandExecutor_ = commandExecutor ?? MissingCommandExecutor.Instance;
    }

    public GuiTreeSnapshot? CurrentTree
    {
        get => currentTree_;
        private set => SetProperty(ref currentTree_, value);
    }

    public GuiTreeValidationResult LastValidationResult
    {
        get => lastValidationResult;
        private set => SetProperty(ref lastValidationResult, value);
    }

    public EditorPanelFrameUpdateRequest FrameUpdateRequest => panel_.FrameUpdateRequest;

    public GuiEventQueue Events => events_;

    public GuiStateStore StateStore => stateStore_;

    internal void ClickButton(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        events_.EnqueueButtonClicked(nodeId);
        Rebuild();
    }

    internal void SelectListItem(GuiNodeId nodeId, string itemId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(itemId))
        {
            throw new ArgumentException("Item id must not be empty.", nameof(itemId));
        }

        ThrowIfDisposed();
        stateStore_.SetSelectedItem(nodeId, itemId);
        Rebuild();
    }

    internal void ResizeSplit(GuiNodeId nodeId, double ratio)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetSplitRatio(nodeId, ratio);
    }

    internal void SetText(GuiNodeId nodeId, string text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetText(nodeId, text ?? string.Empty);
    }

    internal void CommitText(GuiNodeId nodeId, string text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetText(nodeId, text ?? string.Empty);
        Rebuild();
    }

    internal void SetToggle(GuiNodeId nodeId, bool isChecked)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetToggle(nodeId, isChecked);
        Rebuild();
    }

    internal void SetFoldoutExpanded(GuiNodeId nodeId, bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        ThrowIfDisposed();

        stateStore_.SetFoldoutExpanded(nodeId, isExpanded);
        Rebuild();
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
        Rebuild();
    }

    public void OnPanelActivated(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ThrowIfDisposed();

        lifecycleContext_ = context;
        Rebuild();
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
            Rebuild();
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

    private void Rebuild()
    {
        if (lifecycleContext_ is null)
        {
            return;
        }

        var builder = new GuiFrameBuilder(lifecycleContext_.PanelId);
        var gui = new EditorGui(builder, events_, stateStore_, commandExecutor_);
        panel_.DispatchGui(gui);
        var tree = builder.Build();
        var validation = validator_.Validate(tree);
        LastValidationResult = validation;
        if (validation.IsValid)
        {
            CurrentTree = tree;
        }
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

    void IGuiAvaloniaHost.ResizeSplit(GuiNodeId nodeId, double ratio)
    {
        ResizeSplit(nodeId, ratio);
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

        public WorkbenchCommandExecutionResult Execute(string commandId)
        {
            return WorkbenchCommandExecutionResult.NotFound(commandId);
        }
    }
}

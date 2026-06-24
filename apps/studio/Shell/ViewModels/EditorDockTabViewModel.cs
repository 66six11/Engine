using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockTabViewModel : ViewModelBase
{
    private bool isActive_;
    private bool isDragSource_;
    private DockArea area_;
    private IDisposable? panelInstanceRelease_;
    private bool isPanelAttached_;
    private bool isPanelActive_;
    private bool isFloatingWorkspace_;

    public EditorDockTabViewModel(
        string id,
        string title,
        string tag,
        string titleDetail,
        string statusText,
        PanelKind kind,
        DockArea area,
        object content,
        string? iconKey = null,
        IDisposable? panelInstanceRelease = null)
    {
        Id = id;
        Title = title;
        Tag = tag;
        TitleDetail = titleDetail;
        StatusText = statusText;
        Kind = kind;
        area_ = area;
        Content = content;
        panelInstanceRelease_ = panelInstanceRelease;
        IconKey = iconKey;
    }

    public string Id { get; }

    public string Title { get; }

    public string Tag { get; }

    public string TitleDetail { get; }

    public string StatusText { get; }

    public string? IconKey { get; }

    public bool HasIcon => !string.IsNullOrWhiteSpace(IconKey);

    public PanelKind Kind { get; }

    public object Content { get; }

    public DockArea Area
    {
        get => area_;
        set => SetProperty(ref area_, value);
    }

    public bool IsActive
    {
        get => isActive_;
        set => SetProperty(ref isActive_, value);
    }

    public bool IsDragSource
    {
        get => isDragSource_;
        private set => SetProperty(ref isDragSource_, value);
    }

    internal void SetDragSourceState(bool isDragSource)
    {
        IsDragSource = isDragSource;
    }

    internal void ReleasePanelInstance()
    {
        var release = panelInstanceRelease_;
        panelInstanceRelease_ = null;
        DetachPanelInstance();
        release?.Dispose();
    }

    internal void AttachPanelInstance(bool isFloatingWorkspace)
    {
        isFloatingWorkspace_ = isFloatingWorkspace;
        if (isPanelAttached_)
        {
            return;
        }

        isPanelAttached_ = true;
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelAttached(CreateLifecycleContext());
        }
    }

    internal void SetPanelLifecycleHostKind(bool isFloatingWorkspace)
    {
        isFloatingWorkspace_ = isFloatingWorkspace;
    }

    internal void ActivatePanelInstance()
    {
        if (!isPanelAttached_ || isPanelActive_)
        {
            return;
        }

        isPanelActive_ = true;
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelActivated(CreateLifecycleContext());
        }
    }

    internal void DeactivatePanelInstance()
    {
        if (!isPanelActive_)
        {
            return;
        }

        isPanelActive_ = false;
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelDeactivated(CreateLifecycleContext());
        }
    }

    private void DetachPanelInstance()
    {
        if (!isPanelAttached_)
        {
            return;
        }

        DeactivatePanelInstance();
        isPanelAttached_ = false;
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelDetached(CreateLifecycleContext());
        }
    }

    private EditorPanelLifecycleContext CreateLifecycleContext()
    {
        return new EditorPanelLifecycleContext(
            Id,
            Title,
            Area,
            isFloatingWorkspace_);
    }
}

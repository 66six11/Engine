namespace Editor.Shell.ViewModels;

public sealed class EditorDockTabStripItemViewModel : ViewModelBase
{
    private bool isPlaceholder_;
    private bool isPreview_;
    private bool isSourceGhost_;
    private bool isWindowFocused_;

    public EditorDockTabStripItemViewModel(
        EditorDockTabViewModel tab,
        bool isPlaceholder,
        bool isPreview = false,
        bool isSourceGhost = false)
    {
        Tab = tab;
        isPlaceholder_ = isPlaceholder;
        isPreview_ = isPreview;
        isSourceGhost_ = isSourceGhost;
    }

    public EditorDockTabViewModel Tab { get; }

    public bool IsPlaceholder
    {
        get => isPlaceholder_;
        private set
        {
            if (SetProperty(ref isPlaceholder_, value))
            {
                OnPropertyChanged(nameof(IsRealTab));
                OnPropertyChanged(nameof(IsInteractiveTab));
            }
        }
    }

    public bool IsPreview
    {
        get => isPreview_;
        private set
        {
            if (SetProperty(ref isPreview_, value))
            {
                OnPropertyChanged(nameof(IsInteractiveTab));
            }
        }
    }

    public bool IsSourceGhost
    {
        get => isSourceGhost_;
        private set
        {
            if (SetProperty(ref isSourceGhost_, value))
            {
                OnPropertyChanged(nameof(IsInteractiveTab));
            }
        }
    }

    public bool IsRealTab => !IsPlaceholder;

    public bool IsInteractiveTab => IsRealTab && !IsPreview && !IsSourceGhost;

    public bool IsSelectedInFocusedWindow => ShowsSelectionIndicator && isWindowFocused_;

    public bool IsSelectedInInactiveWindow => ShowsSelectionIndicator && !isWindowFocused_;

    internal void SetPresentation(
        bool isPlaceholder,
        bool isPreview = false,
        bool isSourceGhost = false)
    {
        IsPlaceholder = isPlaceholder;
        IsPreview = isPreview;
        IsSourceGhost = isSourceGhost;
        OnSelectionIndicatorStateChanged();
    }

    internal void SetWindowFocusState(bool isWindowFocused)
    {
        if (SetProperty(ref isWindowFocused_, isWindowFocused, nameof(IsWindowFocused)))
        {
            OnSelectionIndicatorStateChanged();
        }
    }

    internal void RefreshSelectionState()
    {
        OnSelectionIndicatorStateChanged();
    }

    private bool IsWindowFocused => isWindowFocused_;

    private bool ShowsSelectionIndicator => Tab.IsActive && IsRealTab && !IsPreview && !IsSourceGhost;

    private void OnSelectionIndicatorStateChanged()
    {
        OnPropertyChanged(nameof(IsSelectedInFocusedWindow));
        OnPropertyChanged(nameof(IsSelectedInInactiveWindow));
    }
}

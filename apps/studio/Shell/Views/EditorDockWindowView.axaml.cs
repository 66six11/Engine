using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockWindowView : UserControl
{
    private EditorDockTabViewModel? capturedTab_;

    public EditorDockWindowView()
    {
        InitializeComponent();
    }

    private void OnTabPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (e.Source is not Control source
            || FindTabItemView(source) is not { DataContext: EditorDockTabStripItemViewModel { IsPlaceholder: false } item } tabItem
            || DataContext is not EditorDockWindowViewModel window)
        {
            return;
        }

        var tab = item.Tab;
        var point = e.GetCurrentPoint(tabItem);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        BeginTabInteraction(tab, window, e);
    }

    private void BeginTabInteraction(
        EditorDockTabViewModel tab,
        EditorDockWindowViewModel window,
        PointerPressedEventArgs e)
    {
        window.Activate(tab);
        capturedTab_ = tab;
        e.Pointer.Capture(this);

        if (TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            workspace.BeginTabDrag(tab, workspacePoint);
        }

        e.Handled = true;
    }

    private static EditorDockTabItemView? FindTabItemView(Control source)
    {
        if (source is EditorDockTabItemView tabItem)
        {
            return tabItem;
        }

        return source.FindAncestorOfType<EditorDockTabItemView>();
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            workspace.UpdateTabDrag(workspacePoint);
        }

        e.Handled = true;
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            workspace.CompleteTabDrag(workspacePoint);
        }

        ClearCapture(e.Pointer);
        e.Handled = true;
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        base.OnPointerCaptureLost(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (TryGetWorkspace(e, out var workspace, out _))
        {
            workspace.CancelTabDrag();
        }

        capturedTab_ = null;
    }

    private bool TryGetWorkspace(RoutedEventArgs args, out EditorDockWorkspaceView workspace, out Point point)
    {
        var ancestor = this.FindAncestorOfType<EditorDockWorkspaceView>();
        if (ancestor is null)
        {
            workspace = null!;
            point = default;
            return false;
        }

        workspace = ancestor;
        point = args switch
        {
            PointerEventArgs pointerArgs => pointerArgs.GetPosition(workspace),
            _ => default,
        };

        return true;
    }

    private void ClearCapture(IPointer pointer)
    {
        capturedTab_ = null;
        pointer.Capture(null);
    }
}

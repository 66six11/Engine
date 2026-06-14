using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockPaneView : UserControl
{
    private EditorDockTabViewModel? capturedTab_;

    public EditorDockPaneView()
    {
        InitializeComponent();
    }

    private void OnTabPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control
            || control.DataContext is not EditorDockTabViewModel tab
            || DataContext is not EditorDockPaneViewModel pane)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        pane.Activate(tab);
        capturedTab_ = tab;
        e.Pointer.Capture(control);

        if (TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            workspace.BeginTabDrag(tab, workspacePoint);
        }

        e.Handled = true;
    }

    private void OnTabPointerMoved(object? sender, PointerEventArgs e)
    {
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

    private void OnTabPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
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

    private void OnTabPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
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
        pointer.Capture(null);
        capturedTab_ = null;
    }
}

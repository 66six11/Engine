using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockTabItemView : UserControl
{
    public EditorDockTabItemView()
    {
        InitializeComponent();
    }

    private void OnCloseButtonClick(object? sender, RoutedEventArgs e)
    {
        e.Handled = true;

        if (DataContext is not EditorDockTabStripItemViewModel { IsPlaceholder: false } item
            || this.FindAncestorOfType<EditorDockWorkspaceView>() is not { } workspace)
        {
            return;
        }

        workspace.CloseTab(item.Tab);
    }
}

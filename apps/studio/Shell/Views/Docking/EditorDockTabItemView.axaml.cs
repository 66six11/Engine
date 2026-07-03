using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Views.Docking;

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

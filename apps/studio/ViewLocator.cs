using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Editor.Shell.ViewModels;
using Editor.Shell.Views;

namespace Editor;

public class ViewLocator : IDataTemplate
{
    public Control? Build(object? param)
    {
        return param switch
        {
            null => null,
            EditorDockSplitNodeViewModel => new EditorDockSplitNodeView(),
            EditorDockWindowNodeViewModel => new EditorDockWindowNodeView(),
            PanelPlaceholderViewModel => new PanelPlaceholderView(),
            _ => new TextBlock { Text = "Not Found: " + param.GetType().Name },
        };
    }

    public bool Match(object? data)
    {
        return data is ViewModelBase;
    }
}

using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Editor.Features.Console.ViewModels;
using Editor.Features.Console.Views;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Hierarchy.Views;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Inspector.Views;
using Editor.Features.Problems.ViewModels;
using Editor.Features.Problems.Views;
using Editor.Features.SceneView.ViewModels;
using Editor.Features.SceneView.Views;
using Editor.Shell.CodeFirstUI;
using Editor.Shell.CodeFirstUI.Views;
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
            SceneViewPanelViewModel => new SceneViewPanelView(),
            HierarchyPanelViewModel => new HierarchyPanelView(),
            InspectorPanelViewModel => new InspectorPanelView(),
            ConsolePanelViewModel => new ConsolePanelView(),
            ProblemsPanelViewModel => new ProblemsPanelView(),
            CodeFirstPanelHostViewModel => new CodeFirstPanelHostView(),
            PanelPlaceholderViewModel => new PanelPlaceholderView(),
            _ => new TextBlock { Text = "Not Found: " + param.GetType().Name },
        };
    }

    public bool Match(object? data)
    {
        return data is ViewModelBase;
    }
}

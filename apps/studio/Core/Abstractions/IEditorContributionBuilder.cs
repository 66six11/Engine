using Editor.Core.Models.Panels;
using Editor.Core.Models.Scene;
using Editor.Core.Models.Workbench;

namespace Editor.Core.Abstractions;

public interface IEditorContributionBuilder
{
    void AddPanel(PanelDescriptor descriptor);

    void AddAction(WorkbenchActionDescriptor descriptor);

    void AddSceneProvider(SceneProviderDescriptor descriptor);
}

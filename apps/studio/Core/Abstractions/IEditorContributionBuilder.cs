using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorContributionBuilder
{
    void AddPanel(PanelDescriptor descriptor);

    void AddAction(WorkbenchActionDescriptor descriptor);

    void AddSceneProvider(SceneProviderDescriptor descriptor);
}

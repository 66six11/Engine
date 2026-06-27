using Editor.Core.CodeFirstUI;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal interface IGuiAvaloniaHost
{
    void ClickButton(GuiNodeId nodeId);

    void SelectListItem(GuiNodeId nodeId, string itemId);
}

using Editor.Core.CodeFirstUI;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal interface IGuiAvaloniaHost
{
    void ClickButton(GuiNodeId nodeId);

    void SelectListItem(GuiNodeId nodeId, string itemId);

    void SelectComboBoxItem(GuiNodeId nodeId, string itemId);

    void SelectRadioGroupItem(GuiNodeId nodeId, string itemId);

    void SelectNavigationRoute(GuiNodeId nodeId, string route);

    void SetNavigationRouteExpanded(GuiNodeId nodeId, string route, bool isExpanded);

    void ResizeSplit(GuiNodeId nodeId, double ratio);

    void SetSliderValue(GuiNodeId nodeId, double value);

    void SetNumberInputValue(GuiNodeId nodeId, double value);

    void SetColorValue(GuiNodeId nodeId, GuiColorValue value);

    void SetText(GuiNodeId nodeId, string text);

    void CommitText(GuiNodeId nodeId, string text);

    void SetToggle(GuiNodeId nodeId, bool isChecked);

    void SetFoldoutExpanded(GuiNodeId nodeId, bool isExpanded);
}

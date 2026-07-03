namespace Editor.Core.CodeFirstUI;

public sealed record GuiTreeSnapshot(
    string PanelId,
    GuiNode Root);

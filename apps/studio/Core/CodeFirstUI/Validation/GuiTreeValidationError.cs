namespace Editor.Core.CodeFirstUI;

public sealed record GuiTreeValidationError(
    GuiTreeValidationErrorCode Code,
    string NodePath,
    string Message);


namespace Editor.Core.CodeFirstUI.Validation;

public sealed record GuiTreeValidationError(
    GuiTreeValidationErrorCode Code,
    string NodePath,
    string Message);

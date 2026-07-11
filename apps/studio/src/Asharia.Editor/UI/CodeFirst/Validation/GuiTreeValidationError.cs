
namespace Asharia.Editor.UI.CodeFirst.Validation;

public sealed record GuiTreeValidationError(
    GuiTreeValidationErrorCode Code,
    string NodePath,
    string Message);

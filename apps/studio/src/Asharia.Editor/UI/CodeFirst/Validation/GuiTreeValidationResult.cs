using System.Collections.Generic;

namespace Asharia.Editor.UI.CodeFirst.Validation;

public sealed record GuiTreeValidationResult(
    IReadOnlyList<GuiTreeValidationError> Errors)
{
    public bool IsValid => Errors.Count == 0;

    public static GuiTreeValidationResult Success { get; } = new([]);
}

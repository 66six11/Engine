using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI.Validation;

public sealed record GuiTreeValidationResult(
    IReadOnlyList<GuiTreeValidationError> Errors)
{
    public bool IsValid => Errors.Count == 0;

    public static GuiTreeValidationResult Success { get; } = new([]);
}

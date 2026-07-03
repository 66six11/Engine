using System.Collections.Generic;

namespace Editor.Core.Models.Contributions;

public sealed record EditorContributionValidationResult(
    IReadOnlyList<EditorContributionValidationError> Errors)
{
    public bool IsValid => Errors.Count == 0;

    public static EditorContributionValidationResult Success { get; } = new([]);
}

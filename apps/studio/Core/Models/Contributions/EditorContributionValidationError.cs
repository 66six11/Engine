namespace Editor.Core.Models.Contributions;

public sealed record EditorContributionValidationError(
    string SourceId,
    string ContributionId,
    string Field,
    string Message);

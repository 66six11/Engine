namespace Editor.Core.Models;

public sealed record EditorContributionValidationError(
    string SourceId,
    string ContributionId,
    string Field,
    string Message);

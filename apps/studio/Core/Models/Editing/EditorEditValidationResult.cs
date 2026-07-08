using System;

namespace Editor.Core.Models.Editing;

public sealed record EditorEditValidationResult
{
    public static EditorEditValidationResult Valid { get; } = new(true, null);

    public EditorEditValidationResult(bool isValid, string? message)
    {
        if (!isValid)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(message);
        }

        IsValid = isValid;
        Message = isValid ? null : message;
    }

    public bool IsValid { get; }

    public string? Message { get; }

    public static EditorEditValidationResult Invalid(string message)
    {
        return new EditorEditValidationResult(false, message);
    }
}

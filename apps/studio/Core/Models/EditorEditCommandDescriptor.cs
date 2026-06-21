using System;

namespace Editor.Core.Models;

public sealed record EditorEditCommandDescriptor
{
    public EditorEditCommandDescriptor(
        string targetId,
        string fieldId,
        string oldValue,
        string newValue,
        string displayLabel,
        EditorEditValidationResult validation,
        EditorEditMergePolicy mergePolicy)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetId);
        ArgumentException.ThrowIfNullOrWhiteSpace(fieldId);
        ArgumentNullException.ThrowIfNull(oldValue);
        ArgumentNullException.ThrowIfNull(newValue);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayLabel);
        ArgumentNullException.ThrowIfNull(validation);

        TargetId = targetId;
        FieldId = fieldId;
        OldValue = oldValue;
        NewValue = newValue;
        DisplayLabel = displayLabel;
        Validation = validation;
        MergePolicy = mergePolicy;
    }

    public string TargetId { get; }

    public string FieldId { get; }

    public string OldValue { get; }

    public string NewValue { get; }

    public string DisplayLabel { get; }

    public EditorEditValidationResult Validation { get; }

    public EditorEditMergePolicy MergePolicy { get; }
}

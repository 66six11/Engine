using System;

namespace Asharia.Editor.Extensions;

public sealed class EditorModuleMetadata
{
    public EditorModuleMetadata(
        EditorModuleDefinitionId definitionId,
        string entryTypeName,
        EditorModuleActivationPolicy activation,
        EditorModuleHandoverPolicy handover)
    {
        if (!definitionId.IsValid)
        {
            throw new ArgumentException(
                "Module definition identity is invalid.",
                nameof(definitionId));
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(entryTypeName);

        if (!Enum.IsDefined(activation))
        {
            throw new ArgumentOutOfRangeException(
                nameof(activation),
                activation,
                "Module activation policy is invalid.");
        }

        if (!Enum.IsDefined(handover))
        {
            throw new ArgumentOutOfRangeException(
                nameof(handover),
                handover,
                "Module handover policy is invalid.");
        }

        DefinitionId = definitionId;
        EntryTypeName = entryTypeName;
        Activation = activation;
        Handover = handover;
    }

    public EditorModuleDefinitionId DefinitionId { get; }

    public string EntryTypeName { get; }

    public EditorModuleActivationPolicy Activation { get; }

    public EditorModuleHandoverPolicy Handover { get; }

    public bool CanReplace(EditorModuleMetadata candidate)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        return DefinitionId == candidate.DefinitionId;
    }
}

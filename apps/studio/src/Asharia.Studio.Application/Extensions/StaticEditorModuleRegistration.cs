using System;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed record StaticEditorModuleRegistration
{
    public StaticEditorModuleRegistration(
        EditorModuleDefinitionId definitionId,
        Func<EditorModule> createDefinition,
        EditorModuleMetadata metadata)
    {
        if (!definitionId.IsValid)
        {
            throw new ArgumentException(
                "Module definition identity is invalid.",
                nameof(definitionId));
        }

        ArgumentNullException.ThrowIfNull(createDefinition);
        ArgumentNullException.ThrowIfNull(metadata);

        if (metadata.DefinitionId != definitionId)
        {
            throw new ArgumentException(
                "Module metadata must describe the registered definition.",
                nameof(metadata));
        }

        DefinitionId = definitionId;
        CreateDefinition = createDefinition;
        Metadata = metadata;
    }

    public EditorModuleDefinitionId DefinitionId { get; }

    public Func<EditorModule> CreateDefinition { get; }

    public EditorModuleMetadata Metadata { get; }
}

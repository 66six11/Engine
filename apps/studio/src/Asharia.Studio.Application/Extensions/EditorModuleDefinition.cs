using System;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorModuleDefinition
{
    internal EditorModuleDefinition(
        EditorModuleMetadata metadata,
        EditorModule module,
        EditorModuleDeclaration declaration)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        ArgumentNullException.ThrowIfNull(module);
        ArgumentNullException.ThrowIfNull(declaration);
        if (metadata.DefinitionId
            != declaration.DefinitionContext.DefinitionId)
        {
            throw new ArgumentException(
                "Module metadata and declaration identities must match.",
                nameof(declaration));
        }

        Metadata = metadata;
        Module = module;
        Declaration = declaration;
    }

    public EditorModuleDefinitionId Id => Metadata.DefinitionId;

    public EditorModuleMetadata Metadata { get; }

    public EditorModule Module { get; }

    public EditorModuleDeclaration Declaration { get; }
}

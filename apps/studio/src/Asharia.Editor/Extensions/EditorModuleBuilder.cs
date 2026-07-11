using System;

namespace Asharia.Editor.Extensions;

public sealed class EditorModuleDefinitionContext
{
    public EditorModuleDefinitionContext(EditorModuleDefinitionId definitionId)
    {
        if (!definitionId.IsValid)
        {
            throw new ArgumentException(
                "Module definition identity is invalid.",
                nameof(definitionId));
        }

        DefinitionId = definitionId;
    }

    public EditorModuleDefinitionId DefinitionId { get; }
}

public sealed class EditorModuleBuilder
{
    public EditorModuleBuilder(EditorModuleDefinitionContext definitionContext)
    {
        ArgumentNullException.ThrowIfNull(definitionContext);
        DefinitionContext = definitionContext;
    }

    public EditorModuleDefinitionContext DefinitionContext { get; }
}

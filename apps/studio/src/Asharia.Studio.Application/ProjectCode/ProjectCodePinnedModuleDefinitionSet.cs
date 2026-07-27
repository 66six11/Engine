using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedModuleDefinitionSet
{
    private ProjectCodePinnedModuleDefinitionSet(
        ProjectCodePinnedModuleConfiguration configuration,
        IReadOnlyList<EditorModuleDefinition> definitions)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        ArgumentNullException.ThrowIfNull(definitions);

        var definitionArray = definitions.ToArray();
        if (definitionArray.Length == 0
            || definitionArray.Length != configuration.Modules.Count)
        {
            throw new ArgumentException(
                "Shared definitions do not match the exact configuration.",
                nameof(definitions));
        }

        var definitionsById =
            new Dictionary<
                EditorModuleDefinitionId,
                EditorModuleDefinition>();
        for (var index = 0; index < definitionArray.Length; ++index)
        {
            var configuredModule = configuration.Modules[index];
            var definition = definitionArray[index]
                ?? throw new ArgumentException(
                    $"Shared definition at index {index} is null.",
                    nameof(definitions));
            if (!ReferenceEquals(
                    definition.Module,
                    configuredModule.ModuleObject.Module)
                || !ReferenceEquals(
                    definition.Metadata,
                    configuredModule.Metadata)
                || !ReferenceEquals(
                    definition.Declaration,
                    configuredModule.Declaration))
            {
                throw new ArgumentException(
                    "Shared definition differs from its exact configured module.",
                    nameof(definitions));
            }

            if (!definitionsById.TryAdd(definition.Id, definition))
            {
                throw new ArgumentException(
                    $"Shared definition '{definition.Id}' is duplicated.",
                    nameof(definitions));
            }
        }

        Configuration = configuration;
        Definitions = Array.AsReadOnly(definitionArray);
        DefinitionsById =
            new ReadOnlyDictionary<
                EditorModuleDefinitionId,
                EditorModuleDefinition>(definitionsById);
    }

    public ProjectCodePinnedModuleConfiguration Configuration { get; }

    public IReadOnlyList<EditorModuleDefinition> Definitions { get; }

    public IReadOnlyDictionary<
        EditorModuleDefinitionId,
        EditorModuleDefinition> DefinitionsById
    {
        get;
    }

    public static ProjectCodePinnedModuleDefinitionSet Create(
        ProjectCodePinnedModuleConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        var definitions = configuration.Modules
            .Select(module => new EditorModuleDefinition(
                module.Metadata,
                module.ModuleObject.Module,
                module.Declaration))
            .ToArray();
        return new ProjectCodePinnedModuleDefinitionSet(
            configuration,
            definitions);
    }
}

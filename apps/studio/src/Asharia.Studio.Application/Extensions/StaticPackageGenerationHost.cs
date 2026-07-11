using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed class StaticPackageGenerationHost
{
    private readonly IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition>
        definitions_;

    private StaticPackageGenerationHost(
        IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> definitions)
    {
        definitions_ = definitions;
    }

    public IReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition> Definitions =>
        definitions_;

    public static StaticPackageGenerationHost Create(
        IEnumerable<StaticEditorModuleRegistration> registrations)
    {
        ArgumentNullException.ThrowIfNull(registrations);

        var registrationArray = registrations.ToArray();
        ValidateRegistrations(registrationArray);

        var definitions = new Dictionary<EditorModuleDefinitionId, EditorModuleDefinition>();
        foreach (var registration in registrationArray)
        {
            var module = registration.CreateDefinition()
                ?? throw new InvalidOperationException(
                    $"Module factory for '{registration.DefinitionId}' returned null.");
            var builder = new EditorModuleBuilder(
                new EditorModuleDefinitionContext(registration.DefinitionId));
            module.Configure(builder);
            var declaration = builder.Build();
            definitions.Add(
                registration.DefinitionId,
                new EditorModuleDefinition(registration, module, declaration));
        }

        return new StaticPackageGenerationHost(
            new ReadOnlyDictionary<EditorModuleDefinitionId, EditorModuleDefinition>(definitions));
    }

    public EditorModuleDefinition GetRequiredDefinition(EditorModuleDefinitionId definitionId)
    {
        if (!definitionId.IsValid)
        {
            throw new ArgumentException(
                "Module definition identity is invalid.",
                nameof(definitionId));
        }

        if (!definitions_.TryGetValue(definitionId, out var definition))
        {
            throw new KeyNotFoundException(
                $"Module definition '{definitionId}' is not registered.");
        }

        return definition;
    }

    private static void ValidateRegistrations(
        IReadOnlyList<StaticEditorModuleRegistration> registrations)
    {
        var definitionIds = new HashSet<EditorModuleDefinitionId>();
        for (var index = 0; index < registrations.Count; index++)
        {
            var registration = registrations[index]
                ?? throw new ArgumentException(
                    $"Module registration at index {index} is null.",
                    nameof(registrations));

            if (!definitionIds.Add(registration.DefinitionId))
            {
                throw new InvalidOperationException(
                    $"Module definition '{registration.DefinitionId}' is already registered.");
            }
        }
    }
}

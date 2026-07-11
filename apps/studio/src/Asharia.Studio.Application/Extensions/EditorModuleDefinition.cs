using System;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorModuleDefinition
{
    internal EditorModuleDefinition(
        StaticEditorModuleRegistration registration,
        EditorModule module,
        EditorModuleDeclaration declaration)
    {
        ArgumentNullException.ThrowIfNull(registration);
        ArgumentNullException.ThrowIfNull(module);
        ArgumentNullException.ThrowIfNull(declaration);

        Registration = registration;
        Module = module;
        Declaration = declaration;
    }

    public EditorModuleDefinitionId Id => Registration.DefinitionId;

    public EditorModuleMetadata Metadata => Registration.Metadata;

    public EditorModule Module { get; }

    public EditorModuleDeclaration Declaration { get; }

    private StaticEditorModuleRegistration Registration { get; }
}

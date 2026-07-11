using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Asharia.Editor.Extensions;

public sealed class EditorModuleDeclaration
{
    internal EditorModuleDeclaration(
        EditorModuleDefinitionContext definitionContext,
        IEnumerable<EditorModuleDefinitionId> requiredModules,
        IEnumerable<EditorModuleDefinitionId> optionalModules,
        IEnumerable<EditorCapabilityId> requiredCapabilities,
        IEnumerable<EditorCapabilityId> optionalCapabilities,
        IEnumerable<EditorCapabilityId> providedCapabilities)
    {
        DefinitionContext = definitionContext;
        RequiredModules = Copy(requiredModules);
        OptionalModules = Copy(optionalModules);
        RequiredCapabilities = Copy(requiredCapabilities);
        OptionalCapabilities = Copy(optionalCapabilities);
        ProvidedCapabilities = Copy(providedCapabilities);
    }

    public EditorModuleDefinitionContext DefinitionContext { get; }

    public IReadOnlyList<EditorModuleDefinitionId> RequiredModules { get; }

    public IReadOnlyList<EditorModuleDefinitionId> OptionalModules { get; }

    public IReadOnlyList<EditorCapabilityId> RequiredCapabilities { get; }

    public IReadOnlyList<EditorCapabilityId> OptionalCapabilities { get; }

    public IReadOnlyList<EditorCapabilityId> ProvidedCapabilities { get; }

    private static ReadOnlyCollection<T> Copy<T>(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return Array.AsReadOnly(values.ToArray());
    }
}

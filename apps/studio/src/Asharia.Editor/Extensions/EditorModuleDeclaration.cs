using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Editor.Panels;

namespace Asharia.Editor.Extensions;

public sealed class EditorModuleDeclaration
{
    internal EditorModuleDeclaration(
        EditorModuleDefinitionContext definitionContext,
        IEnumerable<EditorModuleDefinitionId> requiredModules,
        IEnumerable<EditorModuleDefinitionId> optionalModules,
        IEnumerable<EditorCapabilityId> requiredCapabilities,
        IEnumerable<EditorCapabilityId> optionalCapabilities,
        IEnumerable<EditorCapabilityId> providedCapabilities,
        IEnumerable<EditorPanelDescriptor> panels)
    {
        DefinitionContext = definitionContext;
        RequiredModules = Copy(requiredModules);
        OptionalModules = Copy(optionalModules);
        RequiredCapabilities = Copy(requiredCapabilities);
        OptionalCapabilities = Copy(optionalCapabilities);
        ProvidedCapabilities = Copy(providedCapabilities);
        Panels = Copy(panels);
    }

    public EditorModuleDefinitionContext DefinitionContext { get; }

    public IReadOnlyList<EditorModuleDefinitionId> RequiredModules { get; }

    public IReadOnlyList<EditorModuleDefinitionId> OptionalModules { get; }

    public IReadOnlyList<EditorCapabilityId> RequiredCapabilities { get; }

    public IReadOnlyList<EditorCapabilityId> OptionalCapabilities { get; }

    public IReadOnlyList<EditorCapabilityId> ProvidedCapabilities { get; }

    public IReadOnlyList<EditorPanelDescriptor> Panels { get; }

    private static ReadOnlyCollection<T> Copy<T>(IEnumerable<T> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return Array.AsReadOnly(values.ToArray());
    }
}

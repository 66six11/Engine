using System;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorModuleInstance
{
    internal EditorModuleInstance(
        EditorModuleDefinition definition,
        ScopeInstanceId scopeInstanceId)
    {
        ArgumentNullException.ThrowIfNull(definition);
        if (!scopeInstanceId.IsValid)
        {
            throw new ArgumentException(
                "Scope instance identity is invalid.",
                nameof(scopeInstanceId));
        }

        var isApplicationScope = scopeInstanceId == ScopeInstanceId.Application;
        if ((definition.Id.Scope == EditorModuleScopeKind.Application) != isApplicationScope)
        {
            throw new ArgumentException(
                "Module definition scope does not match the scope instance.",
                nameof(scopeInstanceId));
        }

        Definition = definition;
        Id = EditorModuleInstanceId.Create(definition.Id, scopeInstanceId);
    }

    public EditorModuleInstanceId Id { get; }

    public EditorModuleDefinition Definition { get; }
}

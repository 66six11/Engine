using System;

namespace Asharia.Editor.Extensions;

[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EditorModuleAttribute : Attribute
{
    public EditorModuleAttribute(string id)
    {
        if (!ModuleLocalId.TryCreate(id, out var moduleId))
        {
            throw new ArgumentException(
                "Editor module id must be a lowercase dot-separated namespace.",
                nameof(id));
        }

        Id = moduleId.Value;
    }

    public string Id { get; }

    public EditorModuleScopeKind Scope { get; init; } = EditorModuleScopeKind.Project;

    public EditorModuleActivationPolicy Activation { get; init; } =
        EditorModuleActivationPolicy.OnScopeReady;

    public EditorModuleHandoverPolicy Handover { get; init; } =
        EditorModuleHandoverPolicy.Coexist;
}

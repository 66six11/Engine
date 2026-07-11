using System;

namespace Asharia.Editor.Extensions;

public readonly record struct EditorAssemblyId
{
    private EditorAssemblyId(PackageName package, EditorAssemblyName assembly)
    {
        Package = package;
        Assembly = assembly;
    }

    public PackageName Package { get; }

    public EditorAssemblyName Assembly { get; }

    public bool IsValid => Package.IsValid && Assembly.IsValid;

    public static EditorAssemblyId Create(PackageName package, EditorAssemblyName assembly)
    {
        if (!package.IsValid)
        {
            throw new ArgumentException("Package identity is invalid.", nameof(package));
        }

        if (!assembly.IsValid)
        {
            throw new ArgumentException("Assembly identity is invalid.", nameof(assembly));
        }

        return new EditorAssemblyId(package, assembly);
    }
}

public readonly record struct EditorModuleDefinitionId
{
    private EditorModuleDefinitionId(
        EditorAssemblyId assembly,
        ModuleLocalId module,
        EditorModuleScopeKind scope)
    {
        Assembly = assembly;
        Module = module;
        Scope = scope;
    }

    public EditorAssemblyId Assembly { get; }

    public ModuleLocalId Module { get; }

    public EditorModuleScopeKind Scope { get; }

    public bool IsValid => Assembly.IsValid && Module.IsValid && Enum.IsDefined(Scope);

    public static EditorModuleDefinitionId Create(
        EditorAssemblyId assembly,
        ModuleLocalId module,
        EditorModuleScopeKind scope)
    {
        if (!assembly.IsValid)
        {
            throw new ArgumentException("Editor assembly identity is invalid.", nameof(assembly));
        }

        if (!module.IsValid)
        {
            throw new ArgumentException("Module local identity is invalid.", nameof(module));
        }

        if (!Enum.IsDefined(scope))
        {
            throw new ArgumentOutOfRangeException(nameof(scope), scope, "Module scope is invalid.");
        }

        return new EditorModuleDefinitionId(assembly, module, scope);
    }
}

public readonly record struct EditorModuleInstanceId
{
    private EditorModuleInstanceId(
        EditorModuleDefinitionId definition,
        ScopeInstanceId scopeInstance)
    {
        Definition = definition;
        ScopeInstance = scopeInstance;
    }

    public EditorModuleDefinitionId Definition { get; }

    public ScopeInstanceId ScopeInstance { get; }

    public bool IsValid => Definition.IsValid && ScopeInstance.IsValid;

    public static EditorModuleInstanceId Create(
        EditorModuleDefinitionId definition,
        ScopeInstanceId scopeInstance)
    {
        if (!definition.IsValid)
        {
            throw new ArgumentException("Module definition identity is invalid.", nameof(definition));
        }

        if (!scopeInstance.IsValid)
        {
            throw new ArgumentException("Scope instance identity is invalid.", nameof(scopeInstance));
        }

        return new EditorModuleInstanceId(definition, scopeInstance);
    }
}

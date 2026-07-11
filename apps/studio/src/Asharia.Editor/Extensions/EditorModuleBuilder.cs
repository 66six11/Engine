using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Panels;

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
    private readonly List<EditorModuleDefinitionId> requiredModules_ = [];
    private readonly List<EditorModuleDefinitionId> optionalModules_ = [];
    private readonly List<EditorCapabilityId> requiredCapabilities_ = [];
    private readonly List<EditorCapabilityId> optionalCapabilities_ = [];
    private readonly List<EditorCapabilityId> providedCapabilities_ = [];
    private readonly List<EditorPanelDescriptor> panels_ = [];
    private EditorModuleDeclaration? declaration_;

    public EditorModuleBuilder(EditorModuleDefinitionContext definitionContext)
    {
        ArgumentNullException.ThrowIfNull(definitionContext);
        DefinitionContext = definitionContext;
        Dependencies = new EditorModuleDependencyBuilder(this);
        Capabilities = new EditorModuleCapabilityBuilder(this);
        Panels = new EditorPanelContributionBuilder(this);
    }

    public EditorModuleDefinitionContext DefinitionContext { get; }

    public EditorModuleDependencyBuilder Dependencies { get; }

    public EditorModuleCapabilityBuilder Capabilities { get; }

    public EditorPanelContributionBuilder Panels { get; }

    public EditorModuleDeclaration Build()
    {
        declaration_ ??= new EditorModuleDeclaration(
            DefinitionContext,
            requiredModules_,
            optionalModules_,
            requiredCapabilities_,
            optionalCapabilities_,
            providedCapabilities_,
            panels_);

        return declaration_;
    }

    internal void AddRequiredModule(EditorModuleDefinitionId dependency)
    {
        EnsureMutable();
        ValidateModuleDependency(dependency);
        EnsureModuleNotDeclared(dependency);
        requiredModules_.Add(dependency);
    }

    internal void AddOptionalModule(EditorModuleDefinitionId dependency)
    {
        EnsureMutable();
        ValidateModuleDependency(dependency);
        EnsureModuleNotDeclared(dependency);
        optionalModules_.Add(dependency);
    }

    internal void AddRequiredCapability(EditorCapabilityId capability)
    {
        EnsureMutable();
        ValidateCapability(capability);
        EnsureCapabilityDependencyNotDeclared(capability);
        requiredCapabilities_.Add(capability);
    }

    internal void AddOptionalCapability(EditorCapabilityId capability)
    {
        EnsureMutable();
        ValidateCapability(capability);
        EnsureCapabilityDependencyNotDeclared(capability);
        optionalCapabilities_.Add(capability);
    }

    internal void AddProvidedCapability(EditorCapabilityId capability)
    {
        EnsureMutable();
        ValidateCapability(capability);
        if (providedCapabilities_.Contains(capability))
        {
            throw new InvalidOperationException(
                $"Capability '{capability}' is already provided by this module.");
        }

        providedCapabilities_.Add(capability);
    }

    internal void AddPanel(EditorPanelDescriptor descriptor)
    {
        EnsureMutable();

        if (panels_.Any(panel => panel.Id == descriptor.Id))
        {
            throw new InvalidOperationException(
                $"Panel contribution '{descriptor.Id}' is already declared by this module.");
        }

        if (panels_.Any(panel => panel.ContentFactory == descriptor.ContentFactory))
        {
            throw new InvalidOperationException(
                $"Panel factory '{descriptor.ContentFactory}' is already declared by this module.");
        }

        panels_.Add(descriptor);
    }

    private void EnsureMutable()
    {
        if (declaration_ is not null)
        {
            throw new InvalidOperationException(
                "The module declaration has already been built and cannot be modified.");
        }
    }

    private void ValidateModuleDependency(EditorModuleDefinitionId dependency)
    {
        if (!dependency.IsValid)
        {
            throw new ArgumentException("Module dependency identity is invalid.", nameof(dependency));
        }

        var definition = DefinitionContext.DefinitionId;
        if (dependency == definition)
        {
            throw new InvalidOperationException("A module cannot depend on itself.");
        }

        if (definition.Scope == EditorModuleScopeKind.Application &&
            dependency.Scope == EditorModuleScopeKind.Project)
        {
            throw new InvalidOperationException(
                "An Application-scoped module cannot depend on a Project-scoped module.");
        }
    }

    private void EnsureModuleNotDeclared(EditorModuleDefinitionId dependency)
    {
        if (requiredModules_.Contains(dependency) || optionalModules_.Contains(dependency))
        {
            throw new InvalidOperationException(
                $"Module dependency '{dependency}' is already declared as required or optional.");
        }
    }

    private static void ValidateCapability(EditorCapabilityId capability)
    {
        if (!capability.IsValid)
        {
            throw new ArgumentException("Editor capability identity is invalid.", nameof(capability));
        }
    }

    private void EnsureCapabilityDependencyNotDeclared(EditorCapabilityId capability)
    {
        if (requiredCapabilities_.Contains(capability) || optionalCapabilities_.Contains(capability))
        {
            throw new InvalidOperationException(
                $"Capability dependency '{capability}' is already declared as required or optional.");
        }
    }
}

public sealed class EditorModuleDependencyBuilder
{
    private readonly EditorModuleBuilder owner_;

    internal EditorModuleDependencyBuilder(EditorModuleBuilder owner)
    {
        owner_ = owner;
    }

    public void RequireModule(EditorModuleDefinitionId dependency)
    {
        owner_.AddRequiredModule(dependency);
    }

    public void OptionalModule(EditorModuleDefinitionId dependency)
    {
        owner_.AddOptionalModule(dependency);
    }

    public void RequireCapability(EditorCapabilityId capability)
    {
        owner_.AddRequiredCapability(capability);
    }

    public void OptionalCapability(EditorCapabilityId capability)
    {
        owner_.AddOptionalCapability(capability);
    }
}

public sealed class EditorModuleCapabilityBuilder
{
    private readonly EditorModuleBuilder owner_;

    internal EditorModuleCapabilityBuilder(EditorModuleBuilder owner)
    {
        owner_ = owner;
    }

    public void Provide(EditorCapabilityId capability)
    {
        owner_.AddProvidedCapability(capability);
    }
}

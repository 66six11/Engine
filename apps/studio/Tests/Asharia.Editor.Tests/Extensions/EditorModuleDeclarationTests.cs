using System;
using System.Collections.Generic;
using Asharia.Editor.Extensions;
using Xunit;

namespace Asharia.Editor.Tests.Extensions;

public sealed class EditorModuleDeclarationTests
{
    [Fact]
    public void Builder_freezes_ordered_module_and_capability_declarations()
    {
        var builder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        var applicationModule = CreateDefinition("shell.application", EditorModuleScopeKind.Application);
        var projectModule = CreateDefinition("scene.tools", EditorModuleScopeKind.Project);
        var engineReady = EditorCapabilityId.Create("asharia.project.engine-ready.v1");
        var sourceControl = EditorCapabilityId.Create("asharia.project.source-control.v1");
        var terrainTools = EditorCapabilityId.Create("terrain.tools.v1");

        builder.Dependencies.RequireModule(applicationModule);
        builder.Dependencies.OptionalModule(projectModule);
        builder.Dependencies.RequireCapability(engineReady);
        builder.Dependencies.OptionalCapability(sourceControl);
        builder.Capabilities.Provide(terrainTools);

        var declaration = builder.Build();

        Assert.Same(declaration, builder.Build());
        Assert.Same(builder.DefinitionContext, declaration.DefinitionContext);
        Assert.Equal([applicationModule], declaration.RequiredModules);
        Assert.Equal([projectModule], declaration.OptionalModules);
        Assert.Equal([engineReady], declaration.RequiredCapabilities);
        Assert.Equal([sourceControl], declaration.OptionalCapabilities);
        Assert.Equal([terrainTools], declaration.ProvidedCapabilities);
        AssertReadOnly(declaration.RequiredModules, projectModule);
        AssertReadOnly(declaration.OptionalCapabilities, terrainTools);
    }

    [Fact]
    public void Build_prevents_all_later_mutation()
    {
        var builder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        builder.Build();

        Assert.Throws<InvalidOperationException>(
            () => builder.Dependencies.RequireModule(
                CreateDefinition("shell.application", EditorModuleScopeKind.Application)));
        Assert.Throws<InvalidOperationException>(
            () => builder.Dependencies.OptionalModule(
                CreateDefinition("scene.tools", EditorModuleScopeKind.Project)));
        Assert.Throws<InvalidOperationException>(
            () => builder.Dependencies.RequireCapability(
                EditorCapabilityId.Create("asharia.project.engine-ready.v1")));
        Assert.Throws<InvalidOperationException>(
            () => builder.Dependencies.OptionalCapability(
                EditorCapabilityId.Create("asharia.project.source-control.v1")));
        Assert.Throws<InvalidOperationException>(
            () => builder.Capabilities.Provide(
                EditorCapabilityId.Create("terrain.tools.v1")));
    }

    [Fact]
    public void Builder_rejects_duplicate_and_conflicting_module_dependencies()
    {
        var requiredBuilder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        var dependency = CreateDefinition("scene.tools", EditorModuleScopeKind.Project);
        requiredBuilder.Dependencies.RequireModule(dependency);

        Assert.Throws<InvalidOperationException>(
            () => requiredBuilder.Dependencies.RequireModule(dependency));
        Assert.Throws<InvalidOperationException>(
            () => requiredBuilder.Dependencies.OptionalModule(dependency));

        var optionalBuilder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        optionalBuilder.Dependencies.OptionalModule(dependency);

        Assert.Throws<InvalidOperationException>(
            () => optionalBuilder.Dependencies.OptionalModule(dependency));
        Assert.Throws<InvalidOperationException>(
            () => optionalBuilder.Dependencies.RequireModule(dependency));
    }

    [Fact]
    public void Builder_rejects_duplicate_and_conflicting_capability_declarations()
    {
        var capability = EditorCapabilityId.Create("asharia.project.engine-ready.v1");
        var requiredBuilder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        requiredBuilder.Dependencies.RequireCapability(capability);

        Assert.Throws<InvalidOperationException>(
            () => requiredBuilder.Dependencies.RequireCapability(capability));
        Assert.Throws<InvalidOperationException>(
            () => requiredBuilder.Dependencies.OptionalCapability(capability));

        var optionalBuilder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        optionalBuilder.Dependencies.OptionalCapability(capability);

        Assert.Throws<InvalidOperationException>(
            () => optionalBuilder.Dependencies.OptionalCapability(capability));
        Assert.Throws<InvalidOperationException>(
            () => optionalBuilder.Dependencies.RequireCapability(capability));

        var providerBuilder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);
        providerBuilder.Capabilities.Provide(capability);

        Assert.Throws<InvalidOperationException>(() => providerBuilder.Capabilities.Provide(capability));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Builder_rejects_self_module_dependency(bool optional)
    {
        var builder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);

        Assert.Throws<InvalidOperationException>(() =>
        {
            if (optional)
            {
                builder.Dependencies.OptionalModule(builder.DefinitionContext.DefinitionId);
                return;
            }

            builder.Dependencies.RequireModule(builder.DefinitionContext.DefinitionId);
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Application_module_rejects_project_module_dependency(bool optional)
    {
        var builder = CreateBuilder("shell.application", EditorModuleScopeKind.Application);
        var projectModule = CreateDefinition("terrain.editor", EditorModuleScopeKind.Project);

        Assert.Throws<InvalidOperationException>(() =>
        {
            if (optional)
            {
                builder.Dependencies.OptionalModule(projectModule);
                return;
            }

            builder.Dependencies.RequireModule(projectModule);
        });
    }

    [Fact]
    public void Builder_rejects_invalid_default_identities()
    {
        var builder = CreateBuilder("terrain.editor", EditorModuleScopeKind.Project);

        Assert.Throws<ArgumentException>(
            () => builder.Dependencies.RequireModule(default));
        Assert.Throws<ArgumentException>(
            () => builder.Dependencies.RequireCapability(default));
        Assert.Throws<ArgumentException>(
            () => builder.Capabilities.Provide(default));
    }

    private static EditorModuleBuilder CreateBuilder(
        string moduleId,
        EditorModuleScopeKind scope)
    {
        return new EditorModuleBuilder(
            new EditorModuleDefinitionContext(CreateDefinition(moduleId, scope)));
    }

    private static EditorModuleDefinitionId CreateDefinition(
        string moduleId,
        EditorModuleScopeKind scope)
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("com.asharia.tests"),
                EditorAssemblyName.Create("Asharia.Tests.Editor")),
            ModuleLocalId.Create(moduleId),
            scope);
    }

    private static void AssertReadOnly<T>(IReadOnlyList<T> values, T addedValue)
    {
        var collection = Assert.IsAssignableFrom<ICollection<T>>(values);
        Assert.True(collection.IsReadOnly);
        Assert.Throws<NotSupportedException>(() => collection.Add(addedValue));
    }
}

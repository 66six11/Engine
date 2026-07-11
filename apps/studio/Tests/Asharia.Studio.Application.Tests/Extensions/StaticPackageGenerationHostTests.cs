using System;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;
using Xunit;

namespace Asharia.Studio.Application.Tests.Extensions;

public sealed class StaticPackageGenerationHostTests
{
    [Fact]
    public void Create_constructs_and_configures_each_definition_once()
    {
        var definitionId = CreateDefinitionId("studio.workbench", EditorModuleScopeKind.Application);
        var module = new CountingModule();
        var factoryCalls = 0;
        var registration = CreateRegistration(
            definitionId,
            () =>
            {
                factoryCalls++;
                return module;
            });

        var host = StaticPackageGenerationHost.Create([registration]);

        var first = host.GetRequiredDefinition(definitionId);
        var second = host.GetRequiredDefinition(definitionId);
        Assert.Same(first, second);
        Assert.Same(module, first.Module);
        Assert.Equal(1, factoryCalls);
        Assert.Equal(1, module.ConfigureCalls);
        Assert.Equal(definitionId, first.Declaration.DefinitionContext.DefinitionId);
        Assert.Equal([definitionId], host.Definitions.Keys);
    }

    [Fact]
    public void Create_rejects_duplicate_definitions_before_invoking_factories()
    {
        var definitionId = CreateDefinitionId("studio.workbench", EditorModuleScopeKind.Application);
        var factoryCalls = 0;
        var first = CreateRegistration(
            definitionId,
            () =>
            {
                factoryCalls++;
                return new CountingModule();
            });
        var second = CreateRegistration(
            definitionId,
            () =>
            {
                factoryCalls++;
                return new CountingModule();
            });

        var error = Assert.Throws<InvalidOperationException>(
            () => StaticPackageGenerationHost.Create([first, second]));

        Assert.Contains("already registered", error.Message, StringComparison.Ordinal);
        Assert.Equal(0, factoryCalls);
    }

    [Fact]
    public void Registration_rejects_metadata_for_another_definition()
    {
        var definitionId = CreateDefinitionId("studio.workbench", EditorModuleScopeKind.Application);
        var otherId = CreateDefinitionId("studio.scene", EditorModuleScopeKind.Project);
        var metadata = CreateMetadata(otherId);

        Assert.Throws<ArgumentException>(() => new StaticEditorModuleRegistration(
            definitionId,
            static () => new CountingModule(),
            metadata));
    }

    [Fact]
    public void Create_rejects_a_factory_that_returns_null()
    {
        var definitionId = CreateDefinitionId("studio.workbench", EditorModuleScopeKind.Application);
        var registration = CreateRegistration(definitionId, static () => null!);

        var error = Assert.Throws<InvalidOperationException>(
            () => StaticPackageGenerationHost.Create([registration]));

        Assert.Contains("returned null", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Create_does_not_publish_a_host_when_configuration_fails()
    {
        var definitionId = CreateDefinitionId("studio.workbench", EditorModuleScopeKind.Application);
        var registration = CreateRegistration(
            definitionId,
            static () => new ThrowingModule());

        var error = Assert.Throws<InvalidOperationException>(
            () => StaticPackageGenerationHost.Create([registration]));

        Assert.Equal("Configure failed.", error.Message);
    }

    private static StaticEditorModuleRegistration CreateRegistration(
        EditorModuleDefinitionId definitionId,
        Func<EditorModule> factory)
    {
        return new StaticEditorModuleRegistration(
            definitionId,
            factory,
            CreateMetadata(definitionId));
    }

    private static EditorModuleMetadata CreateMetadata(EditorModuleDefinitionId definitionId)
    {
        return new EditorModuleMetadata(
            definitionId,
            "Tests.CountingModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);
    }

    private static EditorModuleDefinitionId CreateDefinitionId(
        string module,
        EditorModuleScopeKind scope)
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("asharia.studio"),
                EditorAssemblyName.Create("Asharia.Studio.BuiltIns")),
            ModuleLocalId.Create(module),
            scope);
    }

    private sealed class CountingModule : EditorModule
    {
        public int ConfigureCalls { get; private set; }

        public override void Configure(EditorModuleBuilder editor)
        {
            ConfigureCalls++;
        }
    }

    private sealed class ThrowingModule : EditorModule
    {
        public override void Configure(EditorModuleBuilder editor)
        {
            throw new InvalidOperationException("Configure failed.");
        }
    }
}

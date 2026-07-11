using System;
using Asharia.Editor.Extensions;
using Xunit;

namespace Asharia.Editor.Tests.Extensions;

public sealed class EditorModuleMetadataTests
{
    [Fact]
    public void Attribute_defaults_to_project_scope_ready_activation_and_coexist_handover()
    {
        var attribute = new EditorModuleAttribute("terrain.editor");

        Assert.Equal("terrain.editor", attribute.Id);
        Assert.Equal(EditorModuleScopeKind.Project, attribute.Scope);
        Assert.Equal(EditorModuleActivationPolicy.OnScopeReady, attribute.Activation);
        Assert.Equal(EditorModuleHandoverPolicy.Coexist, attribute.Handover);
    }

    [Fact]
    public void Attribute_rejects_noncanonical_module_id()
    {
        var exception = Assert.Throws<ArgumentException>(
            () => new EditorModuleAttribute("Terrain.Editor"));

        Assert.Equal("id", exception.ParamName);
    }

    [Fact]
    public void Metadata_replacement_uses_definition_identity_not_entry_type_name()
    {
        var definition = CreateDefinition("terrain.editor");
        var previous = new EditorModuleMetadata(
            definition,
            "Terrain.Editor.LegacyTerrainModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);
        var candidate = new EditorModuleMetadata(
            definition,
            "Terrain.Editor.TerrainModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);

        Assert.True(previous.CanReplace(candidate));
        Assert.NotEqual(previous.EntryTypeName, candidate.EntryTypeName);
    }

    [Fact]
    public void Metadata_replacement_rejects_different_definition_identity()
    {
        var previous = new EditorModuleMetadata(
            CreateDefinition("terrain.editor"),
            "Terrain.Editor.TerrainModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);
        var candidate = new EditorModuleMetadata(
            CreateDefinition("terrain.tools"),
            "Terrain.Editor.TerrainModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);

        Assert.False(previous.CanReplace(candidate));
    }

    [Fact]
    public void Metadata_rejects_invalid_policy_value()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorModuleMetadata(
            CreateDefinition("terrain.editor"),
            "Terrain.Editor.TerrainModule",
            (EditorModuleActivationPolicy)99,
            EditorModuleHandoverPolicy.Coexist));
    }

    private static EditorModuleDefinitionId CreateDefinition(string moduleId)
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("com.asharia.terrain"),
                EditorAssemblyName.Create("Asharia.Terrain.Editor")),
            ModuleLocalId.Create(moduleId),
            EditorModuleScopeKind.Project);
    }
}

using System;
using Asharia.Editor.Extensions;
using Xunit;

namespace Asharia.Editor.Tests.Extensions;

public sealed class EditorModuleIdentityTests
{
    [Theory]
    [InlineData("terrain.editor", true)]
    [InlineData("terrain.brush-tools", true)]
    [InlineData("Terrain.Editor", false)]
    [InlineData("terrain editor", false)]
    [InlineData("terrain", false)]
    [InlineData("terrain..editor", false)]
    public void Module_local_id_uses_lowercase_namespaced_ids(string value, bool valid)
    {
        Assert.Equal(valid, ModuleLocalId.TryCreate(value, out _));
    }

    [Theory]
    [InlineData("com.asharia.terrain", true)]
    [InlineData("com.asharia.asset-core", true)]
    [InlineData("project:123e4567-e89b-12d3-a456-426614174000:editor", true)]
    [InlineData("Com.Asharia.Terrain", false)]
    [InlineData("terrain", false)]
    public void Package_name_is_canonical_lowercase_namespace(string value, bool valid)
    {
        Assert.Equal(valid, PackageName.TryCreate(value, out _));
    }

    [Theory]
    [InlineData("Asharia.Terrain.Editor", true)]
    [InlineData("Asharia_Project_Editor", true)]
    [InlineData("Asharia Project Editor", false)]
    [InlineData("Asharia/Terrain", false)]
    [InlineData("Asharia.Terrain, Version=1.0.0.0", false)]
    public void Assembly_name_is_a_simple_managed_name(string value, bool valid)
    {
        Assert.Equal(valid, EditorAssemblyName.TryCreate(value, out _));
    }

    [Fact]
    public void Default_identity_values_are_invalid_and_render_as_empty()
    {
        Assert.False(default(PackageName).IsValid);
        Assert.False(default(EditorAssemblyName).IsValid);
        Assert.False(default(ModuleLocalId).IsValid);
        Assert.False(default(ScopeInstanceId).IsValid);
        Assert.False(default(EditorAssemblyId).IsValid);
        Assert.False(default(EditorModuleDefinitionId).IsValid);
        Assert.False(default(EditorModuleInstanceId).IsValid);
        Assert.Equal(string.Empty, default(ModuleLocalId).ToString());
    }

    [Fact]
    public void Invalid_create_throws_with_the_parameter_name()
    {
        var exception = Assert.Throws<ArgumentException>(() => ModuleLocalId.Create("Terrain"));
        Assert.Equal("value", exception.ParamName);
    }

    [Fact]
    public void Project_scope_id_uses_canonical_lowercase_uuid()
    {
        var projectId = Guid.Parse("123E4567-E89B-12D3-A456-426614174000");

        var scopeId = ScopeInstanceId.ForProject(projectId);

        Assert.Equal("project:123e4567-e89b-12d3-a456-426614174000", scopeId.Value);
        Assert.True(ScopeInstanceId.TryCreate(scopeId.Value, out var parsed));
        Assert.Equal(scopeId, parsed);
        Assert.Equal("application", ScopeInstanceId.Application.Value);
    }

    [Fact]
    public void Module_instance_identity_includes_scope_instance()
    {
        var definition = CreateDefinitionId();
        var firstScope = ScopeInstanceId.ForProject(Guid.Parse("11111111-1111-1111-1111-111111111111"));
        var secondScope = ScopeInstanceId.ForProject(Guid.Parse("22222222-2222-2222-2222-222222222222"));

        var first = EditorModuleInstanceId.Create(definition, firstScope);
        var second = EditorModuleInstanceId.Create(definition, secondScope);

        Assert.NotEqual(first, second);
        Assert.Equal(definition, first.Definition);
        Assert.Equal(firstScope, first.ScopeInstance);
    }

    private static EditorModuleDefinitionId CreateDefinitionId()
    {
        var assembly = EditorAssemblyId.Create(
            PackageName.Create("com.asharia.terrain"),
            EditorAssemblyName.Create("Asharia.Terrain.Editor"));
        return EditorModuleDefinitionId.Create(
            assembly,
            ModuleLocalId.Create("terrain.editor"),
            EditorModuleScopeKind.Project);
    }
}

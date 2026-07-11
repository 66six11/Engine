using System;
using Asharia.Editor.Extensions;
using Xunit;

namespace Asharia.Editor.Tests.Extensions;

public sealed class EditorModuleContextTests
{
    [Theory]
    [InlineData("asharia.project.engine-ready.v1", true)]
    [InlineData("terrain.brush-library.v2", true)]
    [InlineData("terrain.brush-library", false)]
    [InlineData("Terrain.BrushLibrary.v1", false)]
    [InlineData("terrain.brush-library.v0", false)]
    public void Capability_id_includes_a_positive_contract_major(string value, bool valid)
    {
        Assert.Equal(valid, EditorCapabilityId.TryCreate(value, out _));
    }

    [Fact]
    public void Capability_snapshot_rejects_negative_epoch()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => EditorCapabilitySnapshot.Create(
            EditorCapabilityId.Create("asharia.project.engine-ready.v1"),
            epoch: -1,
            EditorCapabilityState.Ready));
    }

    [Fact]
    public void Default_capability_identity_and_snapshot_are_invalid()
    {
        Assert.False(default(EditorCapabilityId).IsValid);
        Assert.False(default(EditorCapabilitySnapshot).IsValid);
        Assert.Equal(string.Empty, default(EditorCapabilityId).ToString());
    }

    [Fact]
    public void Module_context_copies_capability_snapshots()
    {
        var capabilities = new[]
        {
            EditorCapabilitySnapshot.Create(
                EditorCapabilityId.Create("asharia.project.engine-ready.v1"),
                epoch: 3,
                EditorCapabilityState.Ready),
        };
        var instanceId = CreateInstanceId();

        var context = new EditorModuleContext(instanceId, capabilities);
        capabilities[0] = EditorCapabilitySnapshot.Create(
            EditorCapabilityId.Create("asharia.project.engine-ready.v1"),
            epoch: 4,
            EditorCapabilityState.Recovering);

        Assert.Equal(instanceId, context.InstanceId);
        Assert.Equal(3, context.Capabilities[0].Epoch);
        Assert.Equal(EditorCapabilityState.Ready, context.Capabilities[0].State);
    }

    [Fact]
    public void Builder_exposes_only_immutable_definition_context()
    {
        var definition = CreateDefinitionId();
        var definitionContext = new EditorModuleDefinitionContext(definition);

        var builder = new EditorModuleBuilder(definitionContext);

        Assert.Same(definitionContext, builder.DefinitionContext);
        Assert.Equal(definition, builder.DefinitionContext.DefinitionId);
    }

    [Fact]
    public void Resume_context_carries_reason_scope_and_new_capability_epoch()
    {
        var scope = ScopeInstanceId.ForProject(
            Guid.Parse("123e4567-e89b-12d3-a456-426614174000"));
        var capability = EditorCapabilitySnapshot.Create(
            EditorCapabilityId.Create("asharia.project.engine-ready.v1"),
            epoch: 8,
            EditorCapabilityState.Ready);

        var context = new EditorModuleResumeContext(
            EditorModuleResumeReason.CapabilityRecovered,
            scope,
            [capability]);

        Assert.Equal(EditorModuleResumeReason.CapabilityRecovered, context.Reason);
        Assert.Equal(scope, context.ScopeInstanceId);
        Assert.Equal(8, context.Capabilities[0].Epoch);
    }

    private static EditorModuleInstanceId CreateInstanceId()
    {
        var definition = CreateDefinitionId();
        return EditorModuleInstanceId.Create(
            definition,
            ScopeInstanceId.ForProject(Guid.Parse("123e4567-e89b-12d3-a456-426614174000")));
    }

    private static EditorModuleDefinitionId CreateDefinitionId()
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("com.asharia.terrain"),
                EditorAssemblyName.Create("Asharia.Terrain.Editor")),
            ModuleLocalId.Create("terrain.editor"),
            EditorModuleScopeKind.Project);
    }
}

using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Asharia.Runtime;
using Xunit;

namespace Asharia.Studio.Application.Tests.Runtime;

public sealed class WorldValueContractsTests
{
    [Fact]
    public void World_values_match_the_native_scene_value_layout()
    {
        AssertLayout<EntityId>(
            8,
            (nameof(EntityId.Index), 0),
            (nameof(EntityId.Generation), 4));
        AssertLayout<Float3>(
            12,
            (nameof(Float3.X), 0),
            (nameof(Float3.Y), 4),
            (nameof(Float3.Z), 8));
        AssertLayout<Quaternion>(
            16,
            (nameof(Quaternion.X), 0),
            (nameof(Quaternion.Y), 4),
            (nameof(Quaternion.Z), 8),
            (nameof(Quaternion.W), 12));
        AssertLayout<TransformValue>(
            40,
            (nameof(TransformValue.Position), 0),
            (nameof(TransformValue.Rotation), 12),
            (nameof(TransformValue.Scale), 28));
    }

    [Fact]
    public void World_values_contain_no_managed_references()
    {
        Assert.False(RuntimeHelpers.IsReferenceOrContainsReferences<EntityId>());
        Assert.False(RuntimeHelpers.IsReferenceOrContainsReferences<Float3>());
        Assert.False(RuntimeHelpers.IsReferenceOrContainsReferences<Quaternion>());
        Assert.False(RuntimeHelpers.IsReferenceOrContainsReferences<TransformValue>());
    }

    [Fact]
    public void Entity_id_requires_nonzero_index_and_generation()
    {
        Assert.Equal(default, EntityId.Invalid);
        Assert.False(EntityId.Invalid.IsValid);
        Assert.False(new EntityId(1, 0).IsValid);
        Assert.False(new EntityId(0, 1).IsValid);
        Assert.True(new EntityId(1, 1).IsValid);
    }

    [Fact]
    public void World_values_expose_canonical_identity_values()
    {
        Assert.Equal(new Float3(0.0f, 0.0f, 0.0f), Float3.Zero);
        Assert.Equal(new Float3(1.0f, 1.0f, 1.0f), Float3.One);
        Assert.Equal(new Quaternion(0.0f, 0.0f, 0.0f, 1.0f), Quaternion.Identity);
        Assert.Equal(
            new TransformValue(Float3.Zero, Quaternion.Identity, Float3.One),
            TransformValue.Identity);
    }

    [Fact]
    public void World_values_keep_structural_value_equality()
    {
        var first = new TransformValue(
            new Float3(1.0f, 2.0f, 3.0f),
            new Quaternion(0.1f, 0.2f, 0.3f, 0.9f),
            new Float3(4.0f, 5.0f, 6.0f));
        var second = new TransformValue(
            new Float3(1.0f, 2.0f, 3.0f),
            new Quaternion(0.1f, 0.2f, 0.3f, 0.9f),
            new Float3(4.0f, 5.0f, 6.0f));

        Assert.Equal(first, second);
        Assert.Equal(first.GetHashCode(), second.GetHashCode());
    }

    private static void AssertLayout<T>(
        int expectedSize,
        params (string PropertyName, int Offset)[] properties)
        where T : unmanaged
    {
        Assert.Equal(LayoutKind.Explicit, typeof(T).StructLayoutAttribute?.Value);
        Assert.Equal(expectedSize, Marshal.SizeOf<T>());

        foreach (var (propertyName, expectedOffset) in properties)
        {
            var backingField = typeof(T).GetField(
                $"<{propertyName}>k__BackingField",
                BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.NotNull(backingField);
            Assert.Equal(
                expectedOffset,
                Marshal.OffsetOf<T>(backingField!.Name).ToInt32());
        }
    }
}

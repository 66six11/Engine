using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Asharia.Runtime;
using Asharia.Studio.EngineBridge.Scene;
using Asharia.Studio.EngineBridge.Scene.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Scene;

public sealed class SceneWorldTests
{
    [Fact]
    public void Abi_types_match_native_world_lifecycle_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<SceneNativeAbiHeader>());
        Assert.Equal(0, Marshal.OffsetOf<SceneNativeAbiHeader>("<AbiVersion>k__BackingField").ToInt32());
        Assert.Equal(4, Marshal.OffsetOf<SceneNativeAbiHeader>("<StructSize>k__BackingField").ToInt32());
        Assert.Equal(8, Marshal.SizeOf<SceneNativeWorldCreateRequest>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeWorldCreateRequest>("<Header>k__BackingField").ToInt32());
        Assert.Equal(SceneNativeAbi.Version, SceneNativeWorldCreateRequest.Current.Header.AbiVersion);
        Assert.Equal(
            SceneNativeWorldCreateRequest.StructSize,
            SceneNativeWorldCreateRequest.Current.Header.StructSize);
        Assert.Equal(8, Marshal.SizeOf<EntityId>());
        Assert.Equal(8, Marshal.SizeOf<SceneNativeCreateEntityRequest>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeCreateEntityRequest>(
                "<Header>k__BackingField").ToInt32());
        Assert.Equal(
            SceneNativeAbi.Version,
            SceneNativeCreateEntityRequest.Current.Header.AbiVersion);
        Assert.Equal(
            SceneNativeCreateEntityRequest.StructSize,
            SceneNativeCreateEntityRequest.Current.Header.StructSize);

        var entity = new EntityId(7, 9);
        var entityRequest = SceneNativeEntityRequest.Current(entity);
        Assert.Equal(16, Marshal.SizeOf<SceneNativeEntityRequest>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeEntityRequest>(
                "<Header>k__BackingField").ToInt32());
        Assert.Equal(
            8,
            Marshal.OffsetOf<SceneNativeEntityRequest>(
                "<Entity>k__BackingField").ToInt32());
        Assert.Equal(SceneNativeAbi.Version, entityRequest.Header.AbiVersion);
        Assert.Equal(SceneNativeEntityRequest.StructSize, entityRequest.Header.StructSize);
        Assert.Equal(entity, entityRequest.Entity);

        var transform = new TransformValue(
            new Float3(1.0f, 2.0f, 3.0f),
            Quaternion.Identity,
            new Float3(4.0f, 5.0f, 6.0f));
        var setTransformRequest =
            SceneNativeSetLocalTransformRequest.Current(entity, transform);
        Assert.Equal(12, Marshal.SizeOf<Float3>());
        Assert.Equal(16, Marshal.SizeOf<Quaternion>());
        Assert.Equal(40, Marshal.SizeOf<TransformValue>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<TransformValue>("<Position>k__BackingField").ToInt32());
        Assert.Equal(
            12,
            Marshal.OffsetOf<TransformValue>("<Rotation>k__BackingField").ToInt32());
        Assert.Equal(
            28,
            Marshal.OffsetOf<TransformValue>("<Scale>k__BackingField").ToInt32());
        Assert.Equal(56, Marshal.SizeOf<SceneNativeSetLocalTransformRequest>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeSetLocalTransformRequest>(
                "<Header>k__BackingField").ToInt32());
        Assert.Equal(
            8,
            Marshal.OffsetOf<SceneNativeSetLocalTransformRequest>(
                "<Entity>k__BackingField").ToInt32());
        Assert.Equal(
            16,
            Marshal.OffsetOf<SceneNativeSetLocalTransformRequest>(
                "<Transform>k__BackingField").ToInt32());
        Assert.Equal(SceneNativeAbi.Version, setTransformRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeSetLocalTransformRequest.StructSize,
            setTransformRequest.Header.StructSize);
        Assert.Equal(entity, setTransformRequest.Entity);
        Assert.Equal(transform, setTransformRequest.Transform);

        var stringView = new SceneNativeStringView((nint)0x1234, 17);
        var setNameRequest = SceneNativeSetEntityNameRequest.Current(
            entity,
            stringView.Data,
            stringView.ByteLength);
        Assert.Equal(16, Marshal.SizeOf<SceneNativeStringView>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeStringView>(
                "<Data>k__BackingField").ToInt32());
        Assert.Equal(
            8,
            Marshal.OffsetOf<SceneNativeStringView>(
                "<ByteLength>k__BackingField").ToInt32());
        Assert.Equal(32, Marshal.SizeOf<SceneNativeSetEntityNameRequest>());
        Assert.Equal(
            0,
            Marshal.OffsetOf<SceneNativeSetEntityNameRequest>(
                "<Header>k__BackingField").ToInt32());
        Assert.Equal(
            8,
            Marshal.OffsetOf<SceneNativeSetEntityNameRequest>(
                "<Entity>k__BackingField").ToInt32());
        Assert.Equal(
            16,
            Marshal.OffsetOf<SceneNativeSetEntityNameRequest>(
                "<NameUtf8>k__BackingField").ToInt32());
        Assert.Equal(SceneNativeAbi.Version, setNameRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeSetEntityNameRequest.StructSize,
            setNameRequest.Header.StructSize);
        Assert.Equal(entity, setNameRequest.Entity);
        Assert.Equal(stringView, setNameRequest.NameUtf8);
        Assert.Equal(typeof(uint), Enum.GetUnderlyingType(typeof(SceneNativeStatus)));
        Assert.Equal(0U, (uint)SceneNativeStatus.Success);
        Assert.Equal(1U, (uint)SceneNativeStatus.InvalidArgument);
        Assert.Equal(2U, (uint)SceneNativeStatus.UnsupportedAbi);
        Assert.Equal(3U, (uint)SceneNativeStatus.WrongThread);
        Assert.Equal(4U, (uint)SceneNativeStatus.InternalError);
        Assert.Equal(5U, (uint)SceneNativeStatus.InvalidEntity);
        Assert.Equal(6U, (uint)SceneNativeStatus.EntityCapacityExceeded);
        Assert.Equal(7U, (uint)SceneNativeStatus.InvalidTransform);
        Assert.Equal(8U, (uint)SceneNativeStatus.InvalidUtf8);
        Assert.Equal(9U, (uint)SceneNativeStatus.BufferTooSmall);
    }

    [Fact]
    public void Create_forwards_current_abi_and_publishes_open_world()
    {
        var api = new StubSceneNativeApi();

        using var world = SceneWorld.Create(api);

        Assert.True(world.IsOpen);
        Assert.Equal(1, api.CreateCalls);
        Assert.Equal(SceneNativeAbi.Version, api.LastCreateRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeWorldCreateRequest.StructSize,
            api.LastCreateRequest.Header.StructSize);
    }

    [Fact]
    public void Create_preserves_native_rejection_status()
    {
        var api = new StubSceneNativeApi
        {
            CreateStatus = SceneNativeStatus.UnsupportedAbi,
            CreateHandle = 0,
        };

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            SceneWorld.Create(api));

        Assert.Equal("scene.world.create", exception.Operation);
        Assert.Equal(SceneNativeStatus.UnsupportedAbi, exception.Status);
        Assert.Contains("UnsupportedAbi (2)", exception.Message, StringComparison.Ordinal);
        Assert.Equal(0, api.DestroyCalls);
    }

    [Fact]
    public void Create_preserves_native_binding_failure()
    {
        var expected = new DllNotFoundException("missing scene library");
        var api = new StubSceneNativeApi
        {
            CreateException = expected,
        };

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            SceneWorld.Create(api));

        Assert.Equal("scene.world.create", exception.Operation);
        Assert.Null(exception.Status);
        Assert.Same(expected, exception.InnerException);
        Assert.Contains("missing scene library", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Create_rejects_success_with_null_world_handle()
    {
        var api = new StubSceneNativeApi
        {
            CreateHandle = 0,
        };

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            SceneWorld.Create(api));

        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.Contains("null World handle", exception.Message, StringComparison.Ordinal);
        Assert.Equal(0, api.DestroyCalls);
    }

    [Fact]
    public void Entity_lifecycle_forwards_world_and_current_requests()
    {
        var entity = new EntityId(7, 9);
        var api = new StubSceneNativeApi
        {
            CreatedEntity = entity,
            IsAliveValue = 1,
        };
        using var world = SceneWorld.Create(api);

        var created = world.CreateEntity();
        var isAlive = world.IsAlive(created);
        world.DestroyEntity(created);

        Assert.Equal(entity, created);
        Assert.True(isAlive);
        Assert.Equal(1, api.CreateEntityCalls);
        Assert.Equal(1, api.IsAliveCalls);
        Assert.Equal(1, api.DestroyEntityCalls);
        Assert.Equal(api.CreateHandle, api.LastCreateEntityWorld);
        Assert.Equal(api.CreateHandle, api.LastIsAliveWorld);
        Assert.Equal(api.CreateHandle, api.LastDestroyEntityWorld);
        Assert.Equal(
            SceneNativeCreateEntityRequest.Current,
            api.LastCreateEntityRequest);
        Assert.Equal(entity, api.LastIsAliveRequest.Entity);
        Assert.Equal(entity, api.LastDestroyEntityRequest.Entity);
        Assert.Equal(SceneNativeAbi.Version, api.LastIsAliveRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeEntityRequest.StructSize,
            api.LastDestroyEntityRequest.Header.StructSize);
    }

    [Fact]
    public void Local_transform_round_trip_forwards_world_entity_and_value()
    {
        var entity = new EntityId(7, 9);
        var transform = new TransformValue(
            new Float3(1.0f, 2.0f, 3.0f),
            new Quaternion(0.0f, 0.0f, 0.0f, 1.0f),
            new Float3(-1.0f, 0.0f, 2.0f));
        var api = new StubSceneNativeApi
        {
            LocalTransform = transform,
        };
        using var world = SceneWorld.Create(api);

        var returned = world.GetLocalTransform(entity);
        world.SetLocalTransform(entity, transform);

        Assert.Equal(transform, returned);
        Assert.Equal(1, api.GetLocalTransformCalls);
        Assert.Equal(1, api.SetLocalTransformCalls);
        Assert.Equal(api.CreateHandle, api.LastGetLocalTransformWorld);
        Assert.Equal(api.CreateHandle, api.LastSetLocalTransformWorld);
        Assert.Equal(entity, api.LastGetLocalTransformRequest.Entity);
        Assert.Equal(entity, api.LastSetLocalTransformRequest.Entity);
        Assert.Equal(transform, api.LastSetLocalTransformRequest.Transform);
        Assert.Equal(
            SceneNativeAbi.Version,
            api.LastGetLocalTransformRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeSetLocalTransformRequest.StructSize,
            api.LastSetLocalTransformRequest.Header.StructSize);
    }

    [Fact]
    public void Get_local_transform_preserves_stale_entity_status()
    {
        var api = new StubSceneNativeApi
        {
            GetLocalTransformStatus = SceneNativeStatus.InvalidEntity,
        };
        using var world = SceneWorld.Create(api);
        var entity = new EntityId(3, 4);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetLocalTransform(entity);
        });

        Assert.Equal("scene.world.entity.local-transform.get", exception.Operation);
        Assert.Equal(SceneNativeStatus.InvalidEntity, exception.Status);
        Assert.Equal(entity, api.LastGetLocalTransformRequest.Entity);
    }

    [Fact]
    public void Set_local_transform_preserves_native_validation_status()
    {
        var transform = new TransformValue(
            Float3.Zero,
            new Quaternion(float.NaN, 0.0f, 0.0f, 1.0f),
            Float3.One);
        var api = new StubSceneNativeApi
        {
            SetLocalTransformStatus = SceneNativeStatus.InvalidTransform,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            world.SetLocalTransform(new EntityId(1, 1), transform));

        Assert.Equal("scene.world.entity.local-transform.set", exception.Operation);
        Assert.Equal(SceneNativeStatus.InvalidTransform, exception.Status);
        Assert.Equal(transform, api.LastSetLocalTransformRequest.Transform);
    }

    [Fact]
    public void Get_local_transform_preserves_native_binding_failure()
    {
        var expected = new EntryPointNotFoundException("missing get local transform");
        var api = new StubSceneNativeApi
        {
            GetLocalTransformException = expected,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetLocalTransform(new EntityId(1, 1));
        });

        Assert.Equal("scene.world.entity.local-transform.get", exception.Operation);
        Assert.Null(exception.Status);
        Assert.Same(expected, exception.InnerException);
        Assert.Contains(
            "missing get local transform",
            exception.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Entity_name_round_trip_uses_exact_caller_owned_utf8()
    {
        const string name = "Cube 立方体 🧊";
        var nameUtf8 = Encoding.UTF8.GetBytes(name);
        var entity = new EntityId(7, 9);
        var api = new StubSceneNativeApi
        {
            EntityNameUtf8 = nameUtf8,
        };
        using var world = SceneWorld.Create(api);

        var returned = world.GetEntityName(entity);
        world.SetEntityName(entity, name);

        Assert.Equal(name, returned);
        Assert.Equal(2, api.GetEntityNameCalls);
        Assert.Equal(1, api.GetEntityNameBufferCalls);
        Assert.Equal(1, api.SetEntityNameCalls);
        Assert.Equal(api.CreateHandle, api.LastGetEntityNameWorld);
        Assert.Equal(api.CreateHandle, api.LastSetEntityNameWorld);
        Assert.Equal(entity, api.LastGetEntityNameRequest.Entity);
        Assert.Equal(entity, api.LastSetEntityNameRequest.Entity);
        Assert.Equal((ulong)nameUtf8.Length, api.LastGetEntityNameCapacity);
        Assert.Equal((ulong)nameUtf8.Length, api.LastSetEntityNameRequest.NameUtf8.ByteLength);
        Assert.NotEqual(0, api.LastSetEntityNameRequest.NameUtf8.Data);
        Assert.Equal(nameUtf8, api.LastSetEntityNameUtf8);
        Assert.Equal(
            SceneNativeAbi.Version,
            api.LastSetEntityNameRequest.Header.AbiVersion);
        Assert.Equal(
            SceneNativeSetEntityNameRequest.StructSize,
            api.LastSetEntityNameRequest.Header.StructSize);
    }

    [Fact]
    public void Empty_entity_name_uses_query_only_and_null_zero_input()
    {
        var api = new StubSceneNativeApi();
        using var world = SceneWorld.Create(api);
        var entity = new EntityId(1, 1);

        var returned = world.GetEntityName(entity);
        world.SetEntityName(entity, string.Empty);

        Assert.Equal(string.Empty, returned);
        Assert.Equal(1, api.GetEntityNameCalls);
        Assert.Equal(0, api.GetEntityNameBufferCalls);
        Assert.Equal(1, api.SetEntityNameCalls);
        Assert.Equal(0, api.LastSetEntityNameRequest.NameUtf8.Data);
        Assert.Equal(0UL, api.LastSetEntityNameRequest.NameUtf8.ByteLength);
        Assert.Empty(api.LastSetEntityNameUtf8);
    }

    [Fact]
    public void Set_entity_name_rejects_invalid_or_oversized_managed_text()
    {
        var api = new StubSceneNativeApi();
        using var world = SceneWorld.Create(api);
        var entity = new EntityId(1, 1);

        var invalidException = Assert.Throws<ArgumentException>(() =>
            world.SetEntityName(entity, "\uD800"));
        var oversizedException = Assert.Throws<ArgumentException>(() =>
            world.SetEntityName(entity, new string('x', 4097)));
        var nullException = Assert.Throws<ArgumentNullException>(() =>
            world.SetEntityName(entity, null!));

        Assert.Equal("name", invalidException.ParamName);
        Assert.IsType<EncoderFallbackException>(invalidException.InnerException);
        Assert.Equal("name", oversizedException.ParamName);
        Assert.Equal("name", nullException.ParamName);
        Assert.Equal(0, api.SetEntityNameCalls);

        world.SetEntityName(entity, new string('x', 4096));

        Assert.Equal(1, api.SetEntityNameCalls);
        Assert.Equal(4096UL, api.LastSetEntityNameRequest.NameUtf8.ByteLength);
    }

    [Fact]
    public void Get_entity_name_rejects_oversized_success_length_before_allocation()
    {
        var api = new StubSceneNativeApi
        {
            QueryEntityNameByteLength = 4097,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetEntityName(new EntityId(1, 1));
        });

        Assert.Equal("scene.world.entity.name.get", exception.Operation);
        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.Contains("above the 4096-byte limit", exception.Message, StringComparison.Ordinal);
        Assert.Equal(1, api.GetEntityNameCalls);
        Assert.Equal(0, api.GetEntityNameBufferCalls);
    }

    [Fact]
    public void Get_entity_name_rejects_malformed_native_success_bytes()
    {
        var api = new StubSceneNativeApi
        {
            EntityNameUtf8 = [0xC0, 0x80],
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetEntityName(new EntityId(1, 1));
        });

        Assert.Equal("scene.world.entity.name.get", exception.Operation);
        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.IsType<DecoderFallbackException>(exception.InnerException);
        Assert.Contains("malformed UTF-8", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Get_entity_name_rejects_query_copy_length_change()
    {
        var api = new StubSceneNativeApi
        {
            EntityNameUtf8 = Encoding.UTF8.GetBytes("Cube"),
            CopyEntityNameByteLength = 3,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetEntityName(new EntityId(1, 1));
        });

        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.Contains("changed entity-name length from 4 to 3", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Get_entity_name_preserves_stale_entity_status()
    {
        var api = new StubSceneNativeApi
        {
            GetEntityNameStatus = SceneNativeStatus.InvalidEntity,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetEntityName(new EntityId(3, 4));
        });

        Assert.Equal("scene.world.entity.name.get", exception.Operation);
        Assert.Equal(SceneNativeStatus.InvalidEntity, exception.Status);
        Assert.Equal(1, api.GetEntityNameCalls);
    }

    [Fact]
    public void Set_entity_name_preserves_native_validation_status()
    {
        var api = new StubSceneNativeApi
        {
            SetEntityNameStatus = SceneNativeStatus.InvalidUtf8,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            world.SetEntityName(new EntityId(1, 1), "Cube"));

        Assert.Equal("scene.world.entity.name.set", exception.Operation);
        Assert.Equal(SceneNativeStatus.InvalidUtf8, exception.Status);
        Assert.Equal(Encoding.UTF8.GetBytes("Cube"), api.LastSetEntityNameUtf8);
    }

    [Fact]
    public void Get_entity_name_preserves_native_binding_failure()
    {
        var expected = new EntryPointNotFoundException("missing get entity name");
        var api = new StubSceneNativeApi
        {
            GetEntityNameException = expected,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.GetEntityName(new EntityId(1, 1));
        });

        Assert.Equal("scene.world.entity.name.get", exception.Operation);
        Assert.Null(exception.Status);
        Assert.Same(expected, exception.InnerException);
    }

    [Fact]
    public void Create_entity_preserves_native_rejection_status()
    {
        var api = new StubSceneNativeApi
        {
            CreateEntityStatus = SceneNativeStatus.EntityCapacityExceeded,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.CreateEntity();
        });

        Assert.Equal("scene.world.entity.create", exception.Operation);
        Assert.Equal(SceneNativeStatus.EntityCapacityExceeded, exception.Status);
        Assert.Contains("EntityCapacityExceeded (6)", exception.Message, StringComparison.Ordinal);
        Assert.Equal(1, api.CreateEntityCalls);
    }

    [Fact]
    public void Create_entity_rejects_success_with_invalid_id()
    {
        var api = new StubSceneNativeApi
        {
            CreatedEntity = EntityId.Invalid,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.CreateEntity();
        });

        Assert.Equal("scene.world.entity.create", exception.Operation);
        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.Contains("invalid Entity ID", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Destroy_entity_preserves_stale_id_status()
    {
        var api = new StubSceneNativeApi
        {
            DestroyEntityStatus = SceneNativeStatus.InvalidEntity,
        };
        using var world = SceneWorld.Create(api);
        var staleEntity = new EntityId(3, 4);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            world.DestroyEntity(staleEntity));

        Assert.Equal("scene.world.entity.destroy", exception.Operation);
        Assert.Equal(SceneNativeStatus.InvalidEntity, exception.Status);
        Assert.Equal(staleEntity, api.LastDestroyEntityRequest.Entity);
    }

    [Fact]
    public void Is_alive_rejects_malformed_success_output()
    {
        var api = new StubSceneNativeApi
        {
            IsAliveValue = 2,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
            world.IsAlive(new EntityId(1, 1)));

        Assert.Equal("scene.world.entity.is-alive", exception.Operation);
        Assert.Equal(SceneNativeStatus.Success, exception.Status);
        Assert.Contains("liveness value 2", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Entity_operations_wrong_thread_fail_before_native_invocation()
    {
        var api = new StubSceneNativeApi();
        using var world = SceneWorld.Create(api);
        var failures = new List<Exception>();
        var entity = new EntityId(1, 1);
        var thread = new Thread(() =>
        {
            foreach (var operation in new Action[]
            {
                () =>
                {
                    _ = world.CreateEntity();
                },
                () => world.DestroyEntity(entity),
                () => world.IsAlive(entity),
                () =>
                {
                    _ = world.GetLocalTransform(entity);
                },
                () => world.SetLocalTransform(entity, TransformValue.Identity),
                () =>
                {
                    _ = world.GetEntityName(entity);
                },
                () => world.SetEntityName(entity, "Cube"),
            })
            {
                try
                {
                    operation();
                }
                catch (Exception exception)
                {
                    failures.Add(exception);
                }
            }
        });

        thread.Start();
        thread.Join();

        Assert.Equal(7, failures.Count);
        Assert.All(failures, failure => Assert.IsType<InvalidOperationException>(failure));
        Assert.Equal(0, api.CreateEntityCalls);
        Assert.Equal(0, api.DestroyEntityCalls);
        Assert.Equal(0, api.IsAliveCalls);
        Assert.Equal(0, api.GetLocalTransformCalls);
        Assert.Equal(0, api.SetLocalTransformCalls);
        Assert.Equal(0, api.GetEntityNameCalls);
        Assert.Equal(0, api.SetEntityNameCalls);
    }

    [Fact]
    public void Entity_operations_on_disposed_world_fail_before_native_invocation()
    {
        var api = new StubSceneNativeApi();
        var world = SceneWorld.Create(api);
        var entity = new EntityId(1, 1);
        world.Dispose();

        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = world.CreateEntity();
        });
        Assert.Throws<ObjectDisposedException>(() => world.DestroyEntity(entity));
        Assert.Throws<ObjectDisposedException>(() => world.IsAlive(entity));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = world.GetLocalTransform(entity);
        });
        Assert.Throws<ObjectDisposedException>(() =>
            world.SetLocalTransform(entity, TransformValue.Identity));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = world.GetEntityName(entity);
        });
        Assert.Throws<ObjectDisposedException>(() =>
            world.SetEntityName(entity, "Cube"));
        Assert.Equal(0, api.CreateEntityCalls);
        Assert.Equal(0, api.DestroyEntityCalls);
        Assert.Equal(0, api.IsAliveCalls);
        Assert.Equal(0, api.GetLocalTransformCalls);
        Assert.Equal(0, api.SetLocalTransformCalls);
        Assert.Equal(0, api.GetEntityNameCalls);
        Assert.Equal(0, api.SetEntityNameCalls);
    }

    [Fact]
    public void Create_entity_preserves_native_binding_failure()
    {
        var expected = new EntryPointNotFoundException("missing create entity");
        var api = new StubSceneNativeApi
        {
            CreateEntityException = expected,
        };
        using var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(() =>
        {
            _ = world.CreateEntity();
        });

        Assert.Equal("scene.world.entity.create", exception.Operation);
        Assert.Null(exception.Status);
        Assert.Same(expected, exception.InnerException);
        Assert.Contains("missing create entity", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Invalid_entity_id_is_not_sent_to_native()
    {
        var api = new StubSceneNativeApi();
        using var world = SceneWorld.Create(api);

        Assert.False(world.IsAlive(EntityId.Invalid));
        var destroyException = Assert.Throws<ArgumentException>(() =>
            world.DestroyEntity(EntityId.Invalid));
        var getException = Assert.Throws<ArgumentException>(() =>
        {
            _ = world.GetLocalTransform(EntityId.Invalid);
        });
        var setException = Assert.Throws<ArgumentException>(() =>
            world.SetLocalTransform(EntityId.Invalid, TransformValue.Identity));
        var getNameException = Assert.Throws<ArgumentException>(() =>
        {
            _ = world.GetEntityName(EntityId.Invalid);
        });
        var setNameException = Assert.Throws<ArgumentException>(() =>
            world.SetEntityName(EntityId.Invalid, "Cube"));

        Assert.Equal("entity", destroyException.ParamName);
        Assert.Equal("entity", getException.ParamName);
        Assert.Equal("entity", setException.ParamName);
        Assert.Equal("entity", getNameException.ParamName);
        Assert.Equal("entity", setNameException.ParamName);
        Assert.Equal(0, api.DestroyEntityCalls);
        Assert.Equal(0, api.IsAliveCalls);
        Assert.Equal(0, api.GetLocalTransformCalls);
        Assert.Equal(0, api.SetLocalTransformCalls);
        Assert.Equal(0, api.GetEntityNameCalls);
        Assert.Equal(0, api.SetEntityNameCalls);
    }

    [Fact]
    public void Dispose_releases_world_exactly_once()
    {
        var api = new StubSceneNativeApi();
        var world = SceneWorld.Create(api);

        world.Dispose();
        world.Dispose();

        Assert.False(world.IsOpen);
        Assert.Equal(1, api.DestroyCalls);
        Assert.Equal(api.CreateHandle, api.LastDestroyedHandle);
    }

    [Fact]
    public void Dispose_wrong_thread_does_not_call_native_or_lose_owner()
    {
        var api = new StubSceneNativeApi();
        var world = SceneWorld.Create(api);
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                world.Dispose();
            }
            catch (Exception exception)
            {
                failure = exception;
            }
        });

        thread.Start();
        thread.Join();

        Assert.IsType<InvalidOperationException>(failure);
        Assert.True(world.IsOpen);
        Assert.Equal(0, api.DestroyCalls);

        world.Dispose();

        Assert.False(world.IsOpen);
        Assert.Equal(1, api.DestroyCalls);
    }

    [Fact]
    public void Dispose_native_failure_keeps_world_open_for_retry()
    {
        var api = new StubSceneNativeApi();
        api.DestroyStatuses.Enqueue(SceneNativeStatus.InternalError);
        api.DestroyStatuses.Enqueue(SceneNativeStatus.Success);
        var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(world.Dispose);

        Assert.Equal("scene.world.destroy", exception.Operation);
        Assert.Equal(SceneNativeStatus.InternalError, exception.Status);
        Assert.True(world.IsOpen);

        world.Dispose();

        Assert.False(world.IsOpen);
        Assert.Equal(2, api.DestroyCalls);
    }

    [Fact]
    public void Dispose_binding_failure_keeps_world_open_for_retry()
    {
        var expected = new EntryPointNotFoundException("missing destroy");
        var api = new StubSceneNativeApi
        {
            DestroyException = expected,
        };
        var world = SceneWorld.Create(api);

        var exception = Assert.Throws<SceneNativeCallException>(world.Dispose);

        Assert.Null(exception.Status);
        Assert.Same(expected, exception.InnerException);
        Assert.True(world.IsOpen);

        api.DestroyException = null;
        world.Dispose();

        Assert.False(world.IsOpen);
        Assert.Equal(2, api.DestroyCalls);
    }

    private sealed class StubSceneNativeApi : ISceneNativeApi
    {
        public int CreateCalls { get; private set; }

        public int DestroyCalls { get; private set; }

        public int CreateEntityCalls { get; private set; }

        public int DestroyEntityCalls { get; private set; }

        public int IsAliveCalls { get; private set; }

        public int GetLocalTransformCalls { get; private set; }

        public int SetLocalTransformCalls { get; private set; }

        public int GetEntityNameCalls { get; private set; }

        public int GetEntityNameBufferCalls { get; private set; }

        public int SetEntityNameCalls { get; private set; }

        public SceneNativeWorldCreateRequest LastCreateRequest { get; private set; }

        public SceneNativeCreateEntityRequest LastCreateEntityRequest { get; private set; }

        public SceneNativeEntityRequest LastDestroyEntityRequest { get; private set; }

        public SceneNativeEntityRequest LastIsAliveRequest { get; private set; }

        public SceneNativeEntityRequest LastGetLocalTransformRequest { get; private set; }

        public SceneNativeSetLocalTransformRequest LastSetLocalTransformRequest
        {
            get;
            private set;
        }

        public SceneNativeEntityRequest LastGetEntityNameRequest { get; private set; }

        public SceneNativeSetEntityNameRequest LastSetEntityNameRequest { get; private set; }

        public nint LastDestroyedHandle { get; private set; }

        public nint LastCreateEntityWorld { get; private set; }

        public nint LastDestroyEntityWorld { get; private set; }

        public nint LastIsAliveWorld { get; private set; }

        public nint LastGetLocalTransformWorld { get; private set; }

        public nint LastSetLocalTransformWorld { get; private set; }

        public nint LastGetEntityNameWorld { get; private set; }

        public nint LastSetEntityNameWorld { get; private set; }

        public ulong LastGetEntityNameCapacity { get; private set; }

        public SceneNativeStatus CreateStatus { get; init; } = SceneNativeStatus.Success;

        public nint CreateHandle { get; init; } = (nint)0x1234;

        public SceneNativeStatus CreateEntityStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus DestroyEntityStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus IsAliveStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus GetLocalTransformStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus SetLocalTransformStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus GetEntityNameStatus { get; init; } =
            SceneNativeStatus.Success;

        public SceneNativeStatus SetEntityNameStatus { get; init; } =
            SceneNativeStatus.Success;

        public EntityId CreatedEntity { get; init; } = new(1, 1);

        public uint IsAliveValue { get; init; }

        public TransformValue LocalTransform { get; init; } = TransformValue.Identity;

        public byte[] EntityNameUtf8 { get; init; } = [];

        public byte[] LastSetEntityNameUtf8 { get; private set; } = [];

        public ulong? QueryEntityNameByteLength { get; init; }

        public ulong? CopyEntityNameByteLength { get; init; }

        public Exception? CreateException { get; init; }

        public Exception? CreateEntityException { get; init; }

        public Exception? DestroyEntityException { get; init; }

        public Exception? IsAliveException { get; init; }

        public Exception? GetLocalTransformException { get; init; }

        public Exception? SetLocalTransformException { get; init; }

        public Exception? GetEntityNameException { get; init; }

        public Exception? SetEntityNameException { get; init; }

        public Exception? DestroyException { get; set; }

        public Queue<SceneNativeStatus> DestroyStatuses { get; } = [];

        public SceneNativeStatus CreateWorld(
            in SceneNativeWorldCreateRequest request,
            out nint world)
        {
            CreateCalls++;
            LastCreateRequest = request;
            world = CreateHandle;
            if (CreateException is not null)
            {
                throw CreateException;
            }

            return CreateStatus;
        }

        public SceneNativeStatus DestroyWorld(nint world)
        {
            DestroyCalls++;
            LastDestroyedHandle = world;
            if (DestroyException is not null)
            {
                throw DestroyException;
            }

            return DestroyStatuses.Count > 0
                ? DestroyStatuses.Dequeue()
                : SceneNativeStatus.Success;
        }

        public SceneNativeStatus CreateEntity(
            nint world,
            in SceneNativeCreateEntityRequest request,
            out EntityId entity)
        {
            CreateEntityCalls++;
            LastCreateEntityWorld = world;
            LastCreateEntityRequest = request;
            entity = EntityId.Invalid;
            if (CreateEntityException is not null)
            {
                throw CreateEntityException;
            }

            entity = CreatedEntity;
            return CreateEntityStatus;
        }

        public SceneNativeStatus DestroyEntity(
            nint world,
            in SceneNativeEntityRequest request)
        {
            DestroyEntityCalls++;
            LastDestroyEntityWorld = world;
            LastDestroyEntityRequest = request;
            if (DestroyEntityException is not null)
            {
                throw DestroyEntityException;
            }

            return DestroyEntityStatus;
        }

        public SceneNativeStatus IsAlive(
            nint world,
            in SceneNativeEntityRequest request,
            out uint isAlive)
        {
            IsAliveCalls++;
            LastIsAliveWorld = world;
            LastIsAliveRequest = request;
            isAlive = 0;
            if (IsAliveException is not null)
            {
                throw IsAliveException;
            }

            isAlive = IsAliveValue;
            return IsAliveStatus;
        }

        public SceneNativeStatus GetLocalTransform(
            nint world,
            in SceneNativeEntityRequest request,
            out TransformValue transform)
        {
            GetLocalTransformCalls++;
            LastGetLocalTransformWorld = world;
            LastGetLocalTransformRequest = request;
            transform = default;
            if (GetLocalTransformException is not null)
            {
                throw GetLocalTransformException;
            }

            transform = LocalTransform;
            return GetLocalTransformStatus;
        }

        public SceneNativeStatus SetLocalTransform(
            nint world,
            in SceneNativeSetLocalTransformRequest request)
        {
            SetLocalTransformCalls++;
            LastSetLocalTransformWorld = world;
            LastSetLocalTransformRequest = request;
            if (SetLocalTransformException is not null)
            {
                throw SetLocalTransformException;
            }

            return SetLocalTransformStatus;
        }

        public SceneNativeStatus GetEntityName(
            nint world,
            in SceneNativeEntityRequest request,
            nint nameUtf8,
            ulong nameCapacity,
            out ulong nameByteLength)
        {
            GetEntityNameCalls++;
            LastGetEntityNameWorld = world;
            LastGetEntityNameRequest = request;
            LastGetEntityNameCapacity = nameCapacity;
            nameByteLength = 0;
            if (nameUtf8 != 0)
            {
                GetEntityNameBufferCalls++;
            }

            if (GetEntityNameException is not null)
            {
                throw GetEntityNameException;
            }

            if (GetEntityNameStatus != SceneNativeStatus.Success)
            {
                return GetEntityNameStatus;
            }

            var actualByteLength = (ulong)EntityNameUtf8.Length;
            nameByteLength = nameUtf8 == 0
                ? QueryEntityNameByteLength ?? actualByteLength
                : CopyEntityNameByteLength ?? actualByteLength;
            if (nameUtf8 == 0)
            {
                return SceneNativeStatus.Success;
            }

            if (nameCapacity < actualByteLength)
            {
                return SceneNativeStatus.BufferTooSmall;
            }

            Marshal.Copy(
                EntityNameUtf8,
                0,
                nameUtf8,
                EntityNameUtf8.Length);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus SetEntityName(
            nint world,
            in SceneNativeSetEntityNameRequest request)
        {
            SetEntityNameCalls++;
            LastSetEntityNameWorld = world;
            LastSetEntityNameRequest = request;
            LastSetEntityNameUtf8 = new byte[checked((int)request.NameUtf8.ByteLength)];
            if (LastSetEntityNameUtf8.Length > 0)
            {
                Marshal.Copy(
                    request.NameUtf8.Data,
                    LastSetEntityNameUtf8,
                    0,
                    LastSetEntityNameUtf8.Length);
            }

            if (SetEntityNameException is not null)
            {
                throw SetEntityNameException;
            }

            return SetEntityNameStatus;
        }
    }
}

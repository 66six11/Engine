using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
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

        public SceneNativeWorldCreateRequest LastCreateRequest { get; private set; }

        public nint LastDestroyedHandle { get; private set; }

        public SceneNativeStatus CreateStatus { get; init; } = SceneNativeStatus.Success;

        public nint CreateHandle { get; init; } = (nint)0x1234;

        public Exception? CreateException { get; init; }

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
    }
}

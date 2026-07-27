using System;
using System.Threading;
using Asharia.Runtime;
using Asharia.Studio.EngineBridge.Scene.Abi;

namespace Asharia.Studio.EngineBridge.Scene;

public sealed class SceneWorld : IDisposable
{
    private const string CreateOperation = "scene.world.create";
    private const string DestroyOperation = "scene.world.destroy";
    private const string CreateEntityOperation = "scene.world.entity.create";
    private const string DestroyEntityOperation = "scene.world.entity.destroy";
    private const string IsAliveOperation = "scene.world.entity.is-alive";
    private const string GetLocalTransformOperation = "scene.world.entity.local-transform.get";
    private const string SetLocalTransformOperation = "scene.world.entity.local-transform.set";

    private readonly ISceneNativeApi nativeApi_;
    private readonly Thread ownerThread_;
    private nint handle_;

    private SceneWorld(
        ISceneNativeApi nativeApi,
        nint handle,
        Thread ownerThread)
    {
        nativeApi_ = nativeApi;
        handle_ = handle;
        ownerThread_ = ownerThread;
    }

    public bool IsOpen => handle_ != 0;

    public static SceneWorld Create()
    {
        return Create(SceneNativeLibraryApi.Instance);
    }

    internal static SceneWorld Create(ISceneNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);

        var request = SceneNativeWorldCreateRequest.Current;
        nint handle = 0;
        SceneNativeStatus status;
        try
        {
            status = nativeApi.CreateWorld(in request, out handle);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(CreateOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(CreateOperation, status);
        }

        if (handle == 0)
        {
            throw InvalidSuccessResult(
                CreateOperation,
                "returned a null World handle.");
        }

        return new SceneWorld(nativeApi, handle, Thread.CurrentThread);
    }

    public EntityId CreateEntity()
    {
        var handle = RequireOwnerHandle(CreateEntityOperation);
        var request = SceneNativeCreateEntityRequest.Current;
        EntityId entity;
        SceneNativeStatus status;
        try
        {
            status = nativeApi_.CreateEntity(
                handle,
                in request,
                out entity);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(CreateEntityOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(CreateEntityOperation, status);
        }

        if (!entity.IsValid)
        {
            throw InvalidSuccessResult(
                CreateEntityOperation,
                "returned an invalid Entity ID.");
        }

        return entity;
    }

    public void DestroyEntity(EntityId entity)
    {
        var handle = RequireOwnerHandle(DestroyEntityOperation);
        RequireValidEntity(entity);

        var request = SceneNativeEntityRequest.Current(entity);
        SceneNativeStatus status;
        try
        {
            status = nativeApi_.DestroyEntity(handle, in request);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(DestroyEntityOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(DestroyEntityOperation, status);
        }
    }

    public bool IsAlive(EntityId entity)
    {
        var handle = RequireOwnerHandle(IsAliveOperation);
        if (!entity.IsValid)
        {
            return false;
        }

        var request = SceneNativeEntityRequest.Current(entity);
        uint isAlive;
        SceneNativeStatus status;
        try
        {
            status = nativeApi_.IsAlive(
                handle,
                in request,
                out isAlive);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(IsAliveOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(IsAliveOperation, status);
        }

        if (isAlive > 1)
        {
            throw InvalidSuccessResult(
                IsAliveOperation,
                $"returned invalid liveness value {isAlive}.");
        }

        return isAlive == 1;
    }

    public TransformValue GetLocalTransform(EntityId entity)
    {
        var handle = RequireOwnerHandle(GetLocalTransformOperation);
        RequireValidEntity(entity);

        var request = SceneNativeEntityRequest.Current(entity);
        TransformValue transform;
        SceneNativeStatus status;
        try
        {
            status = nativeApi_.GetLocalTransform(
                handle,
                in request,
                out transform);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(GetLocalTransformOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(GetLocalTransformOperation, status);
        }

        return transform;
    }

    public void SetLocalTransform(
        EntityId entity,
        TransformValue transform)
    {
        var handle = RequireOwnerHandle(SetLocalTransformOperation);
        RequireValidEntity(entity);

        var request = SceneNativeSetLocalTransformRequest.Current(
            entity,
            transform);
        SceneNativeStatus status;
        try
        {
            status = nativeApi_.SetLocalTransform(handle, in request);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(SetLocalTransformOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(SetLocalTransformOperation, status);
        }
    }

    public void Dispose()
    {
        if (handle_ == 0)
        {
            return;
        }

        if (!ReferenceEquals(Thread.CurrentThread, ownerThread_))
        {
            throw new InvalidOperationException(
                "Scene World must be disposed on the thread that created it. "
                + $"Owner managed thread id is {ownerThread_.ManagedThreadId}; "
                + $"current managed thread id is {Environment.CurrentManagedThreadId}.");
        }

        SceneNativeStatus status;
        try
        {
            status = nativeApi_.DestroyWorld(handle_);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(DestroyOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(DestroyOperation, status);
        }

        handle_ = 0;
    }

    private nint RequireOwnerHandle(string operation)
    {
        if (handle_ == 0)
        {
            throw new ObjectDisposedException(
                nameof(SceneWorld),
                $"Scene native operation '{operation}' requires an open World.");
        }

        if (!ReferenceEquals(Thread.CurrentThread, ownerThread_))
        {
            throw new InvalidOperationException(
                $"Scene native operation '{operation}' must run on the thread "
                + "that created its World. "
                + $"Owner managed thread id is {ownerThread_.ManagedThreadId}; "
                + $"current managed thread id is {Environment.CurrentManagedThreadId}.");
        }

        return handle_;
    }

    private static void RequireValidEntity(EntityId entity)
    {
        if (!entity.IsValid)
        {
            throw new ArgumentException(
                "Entity ID must have a non-zero index and generation.",
                nameof(entity));
        }
    }

    private static SceneNativeCallException BindingFailure(
        string operation,
        Exception exception)
    {
        return new SceneNativeCallException(
            operation,
            null,
            $"Scene native operation '{operation}' is unavailable: {exception.Message}",
            exception);
    }

    private static bool IsNativeBindingFailure(Exception exception)
    {
        return exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
    }

    private static SceneNativeCallException StatusFailure(
        string operation,
        SceneNativeStatus status)
    {
        return new SceneNativeCallException(
            operation,
            status,
            $"Scene native operation '{operation}' failed with status "
            + $"{status} ({(uint)status}).");
    }

    private static SceneNativeCallException InvalidSuccessResult(
        string operation,
        string detail)
    {
        return new SceneNativeCallException(
            operation,
            SceneNativeStatus.Success,
            $"Scene native operation '{operation}' reported success but {detail}");
    }
}

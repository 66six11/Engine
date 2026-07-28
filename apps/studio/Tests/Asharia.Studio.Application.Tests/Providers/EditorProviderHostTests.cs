using System;
using System.Collections.Generic;
using Asharia.Editor.Extensions;
using Asharia.Editor.Worlds.Snapshots;
using Asharia.Studio.Application.Providers;
using Xunit;

namespace Asharia.Studio.Application.Tests.Providers;

public sealed class EditorProviderHostTests
{
    private const string ActiveSceneRole = "scene.active";
    private static readonly EditorAssemblyId TestAssemblyId = EditorAssemblyId.Create(
        PackageName.Create("asharia.tests"),
        EditorAssemblyName.Create("Asharia.Studio.Application.Tests"));

    [Fact]
    public void RegisterOwned_records_owner_and_materializes_scene_provider_once_on_demand()
    {
        var owner = CreateOwner("test.owner");
        var provider = CreateProvider();
        var factoryCalls = 0;
        var host = new EditorProviderHost();

        host.RegisterOwned(new EditorSceneProviderRegistration(
            "test.scene",
            ActiveSceneRole,
            () =>
            {
                factoryCalls++;
                return provider;
            }), owner);

        Assert.Equal(EditorProviderState.Created, host.GetStatus("test.scene").State);
        Assert.Same(provider, host.GetRequiredSceneSnapshotProvider(ActiveSceneRole));
        Assert.Same(provider, host.GetRequiredSceneSnapshotProvider(ActiveSceneRole));
        Assert.Equal(1, factoryCalls);
        Assert.Equal(owner, host.GetOwnerId("test.scene"));
        Assert.Equal(EditorProviderState.Ready, host.GetStatus("test.scene").State);
    }

    [Fact]
    public void RegisterOwned_rejects_duplicate_active_scene_role_with_owner_context()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "first.scene",
            ActiveSceneRole,
            CreateProvider), CreateOwner("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.RegisterOwned(new EditorSceneProviderRegistration(
                "second.scene",
                ActiveSceneRole,
                CreateProvider), CreateOwner("test.second")));

        Assert.Equal(
            "Scene provider role 'scene.active' is already registered by 'test.first'; "
            + "new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void RegisterOwned_rejects_duplicate_scene_provider_id_with_owner_context()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "shared.scene",
            ActiveSceneRole,
            CreateProvider), CreateOwner("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.RegisterOwned(new EditorSceneProviderRegistration(
                "shared.scene",
                "scene.preview",
                CreateProvider), CreateOwner("test.second")));

        Assert.Equal(
            "Scene provider id 'shared.scene' is already registered by 'test.first'; "
            + "new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void GetRequiredSceneSnapshotProvider_records_faulted_status_when_factory_fails()
    {
        var expected = new InvalidOperationException("provider failed");
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "faulted.scene",
            ActiveSceneRole,
            () => throw expected), CreateOwner("test.owner"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.GetRequiredSceneSnapshotProvider(ActiveSceneRole));

        Assert.Same(expected, exception.InnerException);
        var status = host.GetStatus("faulted.scene");
        Assert.Equal(EditorProviderState.Faulted, status.State);
        Assert.Equal("provider failed", status.Message);
    }

    [Fact]
    public void GetRequiredSceneSnapshotProvider_records_faulted_status_when_factory_returns_null()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "faulted.scene",
            ActiveSceneRole,
            () => null!), CreateOwner("test.owner"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.GetRequiredSceneSnapshotProvider(ActiveSceneRole));

        Assert.IsType<InvalidOperationException>(exception.InnerException);
        Assert.Equal("Scene provider factory returned null.", exception.InnerException.Message);
        var status = host.GetStatus("faulted.scene");
        Assert.Equal(EditorProviderState.Faulted, status.State);
        Assert.Equal("Scene provider factory returned null.", status.Message);
    }

    [Fact]
    public void Registration_lease_is_idempotent_and_releases_materialized_provider()
    {
        var disposable = new DisposableSceneSnapshotProvider();
        var host = new EditorProviderHost();
        var lease = host.RegisterOwned(new EditorSceneProviderRegistration(
            "test.scene",
            ActiveSceneRole,
            () => disposable), CreateOwner("test.owner"));

        _ = host.GetRequiredSceneSnapshotProvider(ActiveSceneRole);
        lease.Dispose();
        lease.Dispose();

        Assert.True(disposable.IsDisposed);
        Assert.Empty(host.GetSceneProviders());
    }

    [Fact]
    public void Registration_dispose_failure_still_removes_registration_and_clears_provider()
    {
        var expected = new InvalidOperationException("dispose failed");
        var disposable = new DisposableSceneSnapshotProvider(expected);
        var host = new EditorProviderHost();
        var lease = host.RegisterOwned(new EditorSceneProviderRegistration(
            "test.scene",
            ActiveSceneRole,
            () => disposable), CreateOwner("test.owner"));

        _ = host.GetRequiredSceneSnapshotProvider(ActiveSceneRole);

        var exception = Assert.Throws<InvalidOperationException>(lease.Dispose);

        Assert.Same(expected, exception);
        Assert.True(disposable.IsDisposed);
        Assert.Empty(host.GetSceneProviders());
    }

    [Fact]
    public void Host_disposes_materialized_providers_in_reverse_registration_order()
    {
        var disposalOrder = new List<string>();
        var first = new DisposableSceneSnapshotProvider(
            onDispose: () => disposalOrder.Add("first"));
        var second = new DisposableSceneSnapshotProvider(
            onDispose: () => disposalOrder.Add("second"));
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "first.scene",
            ActiveSceneRole,
            () => first), CreateOwner("test.first"));
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "second.scene",
            "scene.preview",
            () => second), CreateOwner("test.second"));
        _ = host.GetRequiredSceneSnapshotProvider(ActiveSceneRole);
        _ = host.GetRequiredSceneSnapshotProvider("scene.preview");

        host.Dispose();
        host.Dispose();

        Assert.Equal(["second", "first"], disposalOrder);
        Assert.Empty(host.GetSceneProviders());
    }

    [Fact]
    public void Host_dispose_collects_failures_and_still_clears_every_registration()
    {
        var disposalOrder = new List<string>();
        var firstFailure = new InvalidOperationException("first failed");
        var secondFailure = new InvalidOperationException("second failed");
        var first = new DisposableSceneSnapshotProvider(
            firstFailure,
            () => disposalOrder.Add("first"));
        var second = new DisposableSceneSnapshotProvider(
            secondFailure,
            () => disposalOrder.Add("second"));
        var host = new EditorProviderHost();
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "first.scene",
            ActiveSceneRole,
            () => first), CreateOwner("test.first"));
        host.RegisterOwned(new EditorSceneProviderRegistration(
            "second.scene",
            "scene.preview",
            () => second), CreateOwner("test.second"));
        _ = host.GetRequiredSceneSnapshotProvider(ActiveSceneRole);
        _ = host.GetRequiredSceneSnapshotProvider("scene.preview");

        var exception = Assert.Throws<AggregateException>(host.Dispose);

        Assert.Equal([secondFailure, firstFailure], exception.InnerExceptions);
        Assert.Equal(["second", "first"], disposalOrder);
        Assert.Empty(host.GetSceneProviders());
        host.Dispose();
    }

    private static EditorModuleDefinitionId CreateOwner(string moduleId)
    {
        return EditorModuleDefinitionId.Create(
            TestAssemblyId,
            ModuleLocalId.Create(moduleId),
            EditorModuleScopeKind.Application);
    }

    private static ISceneSnapshotProvider CreateProvider()
    {
        return new DisposableSceneSnapshotProvider();
    }

    private sealed class DisposableSceneSnapshotProvider(
        Exception? disposeException = null,
        Action? onDispose = null) : ISceneSnapshotProvider, IDisposable
    {
        public bool IsDisposed { get; private set; }

        public event EventHandler? SnapshotChanged
        {
            add
            {
            }
            remove
            {
            }
        }

        public SceneSnapshot GetCurrentSnapshot() => SceneSnapshot.Empty;

        public bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject)
        {
            sceneObject = null;
            return false;
        }

        public void Dispose()
        {
            IsDisposed = true;
            onDispose?.Invoke();
            if (disposeException is not null)
            {
                throw disposeException;
            }
        }
    }
}

using System;
using Editor.Core.Abstractions;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Scene;
using Editor.Core.Services;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorProviderHostTests
{
    [Fact]
    public void RegisterOwned_records_owner_and_materializes_scene_provider_on_demand()
    {
        var owner = new EditorExtensionId("test.owner");
        var provider = CreateProvider();
        var host = new EditorProviderHost();

        host.RegisterOwned(new SceneProviderDescriptor(
            "test.scene",
            EditorProviderRoles.ActiveScene,
            () => provider), owner);

        Assert.Equal(EditorProviderState.Created, host.GetStatus("test.scene").State);
        Assert.Same(provider, host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));
        Assert.Equal(owner, host.GetOwnerId("test.scene"));
        Assert.Equal(EditorProviderState.Ready, host.GetStatus("test.scene").State);
    }

    [Fact]
    public void RegisterOwned_rejects_duplicate_active_scene_role_with_owner_context()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new SceneProviderDescriptor(
            "first.scene",
            EditorProviderRoles.ActiveScene,
            CreateProvider), new EditorExtensionId("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.RegisterOwned(new SceneProviderDescriptor(
                "second.scene",
                EditorProviderRoles.ActiveScene,
                CreateProvider), new EditorExtensionId("test.second")));

        Assert.Equal(
            "Scene provider role 'scene.active' is already registered by 'test.first'; new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void RegisterOwned_rejects_duplicate_scene_provider_id_with_owner_context()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new SceneProviderDescriptor(
            "shared.scene",
            EditorProviderRoles.ActiveScene,
            CreateProvider), new EditorExtensionId("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.RegisterOwned(new SceneProviderDescriptor(
                "shared.scene",
                "scene.preview",
                CreateProvider), new EditorExtensionId("test.second")));

        Assert.Equal(
            "Scene provider id 'shared.scene' is already registered by 'test.first'; new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void GetRequiredSceneSnapshotProvider_records_faulted_status_when_factory_fails()
    {
        var expected = new InvalidOperationException("provider failed");
        var host = new EditorProviderHost();
        host.RegisterOwned(new SceneProviderDescriptor(
            "faulted.scene",
            EditorProviderRoles.ActiveScene,
            () => throw expected), new EditorExtensionId("test.owner"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));

        Assert.Same(expected, exception.InnerException);
        var status = host.GetStatus("faulted.scene");
        Assert.Equal(EditorProviderState.Faulted, status.State);
        Assert.Equal("provider failed", status.Message);
    }

    [Fact]
    public void GetRequiredSceneSnapshotProvider_records_faulted_status_when_factory_returns_null()
    {
        var host = new EditorProviderHost();
        host.RegisterOwned(new SceneProviderDescriptor(
            "faulted.scene",
            EditorProviderRoles.ActiveScene,
            () => null!), new EditorExtensionId("test.owner"));

        var exception = Assert.Throws<InvalidOperationException>(() =>
            host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));

        Assert.IsType<InvalidOperationException>(exception.InnerException);
        Assert.Equal("Scene provider factory returned null.", exception.InnerException.Message);
        var status = host.GetStatus("faulted.scene");
        Assert.Equal(EditorProviderState.Faulted, status.State);
        Assert.Equal("Scene provider factory returned null.", status.Message);
    }

    [Fact]
    public void Dispose_releases_materialized_provider_and_removes_registration()
    {
        var disposable = new DisposableSceneSnapshotProvider();
        var host = new EditorProviderHost();
        var lease = host.RegisterOwned(new SceneProviderDescriptor(
            "test.scene",
            EditorProviderRoles.ActiveScene,
            () => disposable), new EditorExtensionId("test.owner"));

        _ = host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene);
        lease.Dispose();

        Assert.True(disposable.IsDisposed);
        Assert.Empty(host.GetSceneProviders());
    }

    [Fact]
    public void Dispose_failure_still_removes_registration_and_clears_provider()
    {
        var expected = new InvalidOperationException("dispose failed");
        var disposable = new DisposableSceneSnapshotProvider(expected);
        var host = new EditorProviderHost();
        var lease = host.RegisterOwned(new SceneProviderDescriptor(
            "test.scene",
            EditorProviderRoles.ActiveScene,
            () => disposable), new EditorExtensionId("test.owner"));

        _ = host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene);

        var exception = Assert.Throws<InvalidOperationException>(lease.Dispose);

        Assert.Same(expected, exception);
        Assert.True(disposable.IsDisposed);
        Assert.Empty(host.GetSceneProviders());
    }

    private static ISceneSnapshotProvider CreateProvider()
    {
        return new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [
                new SceneObjectSnapshot("scene:test", "Test Scene", "scene"),
            ]));
    }

    private sealed class DisposableSceneSnapshotProvider(Exception? disposeException = null) :
        ISceneSnapshotProvider,
        IDisposable
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

        public SceneSnapshot GetCurrentSnapshot()
        {
            return new SceneSnapshot(
                "scene:test",
                "Test Scene",
                1,
                [
                    new SceneObjectSnapshot("scene:test", "Test Scene", "scene"),
                ]);
        }

        public bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject)
        {
            sceneObject = new SceneObjectSnapshot(objectId, objectId, "test");
            return true;
        }

        public void Dispose()
        {
            IsDisposed = true;
            if (disposeException is not null)
            {
                throw disposeException;
            }
        }
    }
}

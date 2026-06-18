using System;
using System.Collections.Generic;
using Editor.Core.Models;
using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.CoreContracts;

public sealed class SceneSnapshotProviderTests
{
    [Fact]
    public void InMemory_provider_exposes_current_snapshot_and_lookup()
    {
        var cube = new SceneObjectSnapshot(
            "scene:test/cube",
            "Cube",
            "mesh",
            parentId: "scene:test",
            iconKey: "studio.object.default",
            properties:
            [
                new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
            ]);
        var snapshot = new SceneSnapshot("scene:test", "Test Scene", 7, [cube]);
        var provider = new InMemorySceneSnapshotProvider(snapshot);

        Assert.Same(snapshot, provider.Current);
        Assert.True(provider.TryGetObject("scene:test/cube", out var actual));
        Assert.Same(cube, actual);
        Assert.False(provider.TryGetObject("scene:test/missing", out var missing));
        Assert.Null(missing);
        Assert.False(provider.TryGetObject(" ", out var blank));
        Assert.Null(blank);
    }

    [Fact]
    public void InMemory_provider_rejects_duplicate_object_ids()
    {
        var first = new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh");
        var second = new SceneObjectSnapshot("scene:test/cube", "Duplicate Cube", "mesh");
        var snapshot = new SceneSnapshot("scene:test", "Test Scene", 1, [first, second]);

        var exception = Assert.Throws<InvalidOperationException>(
            () => new InMemorySceneSnapshotProvider(snapshot));

        Assert.Contains("scene:test/cube", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Scene_object_snapshot_normalizes_blank_display_name_to_id()
    {
        var sceneObject = new SceneObjectSnapshot("scene:test/cube", " ", "mesh");

        Assert.Equal("scene:test/cube", sceneObject.DisplayName);
    }

    [Fact]
    public void Scene_snapshot_rejects_blank_scene_id()
    {
        var exception = Assert.Throws<ArgumentException>(
            () => new SceneSnapshot(" ", "Test Scene", 1));

        Assert.Equal("id", exception.ParamName);
    }

    [Fact]
    public void Scene_object_snapshot_rejects_required_identity_fields()
    {
        var idException = Assert.Throws<ArgumentException>(
            () => new SceneObjectSnapshot(" ", "Cube", "mesh"));
        var kindException = Assert.Throws<ArgumentException>(
            () => new SceneObjectSnapshot("scene:test/cube", "Cube", " "));

        Assert.Equal("id", idException.ParamName);
        Assert.Equal("kind", kindException.ParamName);
    }

    [Fact]
    public void Scene_object_property_snapshot_rejects_required_fields()
    {
        var idException = Assert.Throws<ArgumentException>(
            () => new SceneObjectPropertySnapshot(" ", "Name", "Cube"));
        var valueException = Assert.Throws<ArgumentNullException>(
            () => new SceneObjectPropertySnapshot("name", "Name", null!));

        Assert.Equal("id", idException.ParamName);
        Assert.Equal("value", valueException.ParamName);
    }

    [Fact]
    public void Scene_object_snapshot_converts_to_selection_item()
    {
        var sceneObject = new SceneObjectSnapshot(
            "scene:test/cube",
            "Cube",
            "mesh",
            iconKey: "studio.object.mesh");

        var item = sceneObject.ToSelectionItem();

        Assert.Equal("scene:test/cube", item.Id);
        Assert.Equal("mesh", item.Kind);
        Assert.Equal("Cube", item.DisplayName);
        Assert.Equal("studio.object.mesh", item.IconKey);
    }

    [Fact]
    public void Scene_object_snapshot_exposes_read_only_property_list()
    {
        var properties = new List<SceneObjectPropertySnapshot>
        {
            new("name", "Name", "Cube"),
        };

        var sceneObject = new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh", properties: properties);
        properties.Add(new SceneObjectPropertySnapshot("kind", "Kind", "mesh"));

        Assert.IsNotType<SceneObjectPropertySnapshot[]>(sceneObject.Properties);
        var property = Assert.Single(sceneObject.Properties);
        Assert.Equal("name", property.Id);
    }

    [Fact]
    public void Scene_snapshot_exposes_read_only_object_list()
    {
        var cube = new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh");
        var objects = new List<SceneObjectSnapshot>
        {
            cube,
        };

        var snapshot = new SceneSnapshot("scene:test", "Test Scene", 1, objects);
        objects.Add(new SceneObjectSnapshot("scene:test/light", "Light", "light"));

        Assert.IsNotType<SceneObjectSnapshot[]>(snapshot.Objects);
        var sceneObject = Assert.Single(snapshot.Objects);
        Assert.Same(cube, sceneObject);
    }
}

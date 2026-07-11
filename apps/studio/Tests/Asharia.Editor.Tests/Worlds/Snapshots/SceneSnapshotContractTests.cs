using System;
using System.Collections.Generic;
using Asharia.Editor.Worlds.Snapshots;
using Xunit;

namespace Asharia.Editor.Tests.Worlds.Snapshots;

public sealed class SceneSnapshotContractTests
{
    [Fact]
    public void Scene_snapshot_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(SceneObjectPropertySnapshot),
            typeof(SceneObjectPropertyValueKind),
            typeof(SceneObjectSnapshot),
            typeof(SceneSnapshot),
            typeof(ISceneSnapshotProvider),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Worlds.Snapshots", type.Namespace));
    }

    [Fact]
    public void Scene_snapshot_freezes_the_input_collection()
    {
        var objects = new List<SceneObjectSnapshot>
        {
            new("scene:test/cube", "Cube", "mesh"),
        };
        var snapshot = new SceneSnapshot("scene:test", "Test", 1, objects);

        objects.Add(new SceneObjectSnapshot("scene:test/light", "Light", "light"));

        Assert.Single(snapshot.Objects);
    }
}

using System.Collections.Generic;
using Editor.Features.Hierarchy.Models;

namespace Editor.Features.Hierarchy.Services;

public sealed class DemoHierarchyDataSource : IHierarchyDataSource
{
    public IReadOnlyList<HierarchyNodeModel> LoadNodes()
    {
        return
        [
            new HierarchyNodeModel("scene:main", "Main Scene", "scene"),
            new HierarchyNodeModel("scene:main-camera", "Main Camera", "camera", ParentId: "scene:main"),
            new HierarchyNodeModel("scene:key-light", "Key Light", "light", ParentId: "scene:main"),
            new HierarchyNodeModel("scene:demo-cube", "Demo Cube", "mesh", ParentId: "scene:main"),
            new HierarchyNodeModel("scene:demo-cube:renderer", "Mesh Renderer", "component", ParentId: "scene:demo-cube"),
            new HierarchyNodeModel("scene:physics-volume", "Physics Volume", "volume", ParentId: "scene:main"),
        ];
    }
}

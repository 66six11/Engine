using System.Collections.Generic;
using Editor.Features.Hierarchy.Models;

namespace Editor.Features.Hierarchy.Services;

public interface IHierarchyDataSource
{
    IReadOnlyList<HierarchyNodeModel> LoadNodes();
}

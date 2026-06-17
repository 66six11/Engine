using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IWorkbenchActionRegistry
{
    void Register(WorkbenchActionDescriptor descriptor);

    IReadOnlyList<WorkbenchActionDescriptor> GetAll();
}

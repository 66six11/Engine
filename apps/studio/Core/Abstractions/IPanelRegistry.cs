using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IPanelRegistry
{
    void Register(PanelDescriptor descriptor);

    IReadOnlyList<PanelDescriptor> GetAll();

    PanelDescriptor GetRequired(string id);
}

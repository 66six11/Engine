using System.Collections.Generic;
using Editor.Core.Models;
using Editor.Core.Models.Panels;

namespace Editor.Core.Abstractions;

public interface IPanelRegistry
{
    void Register(PanelDescriptor descriptor);

    IReadOnlyList<PanelDescriptor> GetAll();

    PanelDescriptor GetRequired(string id);
}

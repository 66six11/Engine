using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorEditCommand
{
    EditorEditCommandDescriptor Descriptor { get; }

    void Apply();

    void Revert();
}

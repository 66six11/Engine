using Editor.Core.Models.Editing;

namespace Editor.Core.Abstractions;

public interface IEditorEditCommand
{
    /// <summary>
    /// Describes the editor-side mutation, including the old/new values used by undo and diagnostics.
    /// </summary>
    EditorEditCommandDescriptor Descriptor { get; }

    /// <summary>
    /// Applies a validated editor-side mutation.
    /// </summary>
    void Apply();

    /// <summary>
    /// Reverts a successful <see cref="Apply"/> call. Implementations are expected to be reliable and
    /// restore editor-side state from <see cref="Descriptor"/> without depending on runtime/native writes.
    /// </summary>
    void Revert();
}

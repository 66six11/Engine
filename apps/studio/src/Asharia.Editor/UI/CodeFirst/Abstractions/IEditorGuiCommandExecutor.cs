using Asharia.Editor.Commands;

namespace Asharia.Editor.UI.CodeFirst.Abstractions;

public interface IEditorGuiCommandExecutor
{
    EditorCommandExecutionResult Execute(string commandId);
}

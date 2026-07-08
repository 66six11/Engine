using Editor.Core.Models.Workbench;

namespace Editor.Core.CodeFirstUI.Abstractions;

public interface IEditorGuiCommandExecutor
{
    WorkbenchCommandExecutionResult Execute(string commandId);
}

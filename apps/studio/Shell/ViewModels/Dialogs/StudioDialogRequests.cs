using Asharia.Editor.Dialogs;

namespace Editor.Shell.ViewModels.Dialogs;

internal static class StudioDialogRequests
{
    public static EditorDialogRequest About()
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "About Studio",
            "Studio editor shell for VkEngine.",
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("close"),
                    "Close",
                    EditorDialogActionRole.Dismiss,
                    isDefault: true),
            ]);
    }
}

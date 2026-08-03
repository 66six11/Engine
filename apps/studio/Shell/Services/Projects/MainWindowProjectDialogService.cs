using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Platform.Storage;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Services.Projects;

internal sealed class MainWindowProjectDialogService : IStudioProjectDialogService
{
    private MainWindow? owner_;

    public void Attach(MainWindow owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        if (owner_ is not null)
        {
            throw new InvalidOperationException(
                "The project dialog service already has an owner window.");
        }

        owner_ = owner;
    }

    public async ValueTask<string?> SelectProjectParentDirectoryAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var folders = await RequireOwner().StorageProvider.OpenFolderPickerAsync(
            new FolderPickerOpenOptions
            {
                Title = "Select a parent folder for the new project",
                AllowMultiple = false,
            });
        cancellationToken.ThrowIfCancellationRequested();
        return LocalPathOrNull(folders.Count == 0 ? null : folders[0]);
    }

    public async ValueTask<string?> SelectProjectDescriptorAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var files = await RequireOwner().StorageProvider.OpenFilePickerAsync(
            new FilePickerOpenOptions
            {
                Title = "Open an Asharia project",
                AllowMultiple = false,
                FileTypeFilter =
                [
                    new FilePickerFileType("Asharia Project")
                    {
                        Patterns = ["asharia.project.json"],
                    },
                ],
            });
        cancellationToken.ThrowIfCancellationRequested();
        return LocalPathOrNull(files.Count == 0 ? null : files[0]);
    }

    private MainWindow RequireOwner() =>
        owner_ ?? throw new InvalidOperationException(
            "The project dialog service has no owner window.");

    private static string? LocalPathOrNull(IStorageItem? item)
    {
        if (item is null)
        {
            return null;
        }

        return item.TryGetLocalPath()
            ?? throw new InvalidOperationException(
                "The selected project location does not expose a local path.");
    }
}

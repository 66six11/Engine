using System.Threading;
using System.Threading.Tasks;

namespace Editor.Shell.Services.Projects;

internal interface IStudioProjectDialogService
{
    ValueTask<string?> SelectProjectParentDirectoryAsync(
        CancellationToken cancellationToken = default);

    ValueTask<string?> SelectProjectDescriptorAsync(
        CancellationToken cancellationToken = default);
}

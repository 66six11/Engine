using System;
using System.Threading.Tasks;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionSession : IAsyncDisposable
{
    private readonly EditorExtensionHost extensionHost_;

    public StudioCompositionSession(
        MainWindowViewModel mainWindowViewModel,
        EditorExtensionComposition composition,
        EditorExtensionHost extensionHost)
    {
        MainWindowViewModel = mainWindowViewModel;
        Composition = composition;
        extensionHost_ = extensionHost;
    }

    public MainWindowViewModel MainWindowViewModel { get; }

    internal EditorExtensionComposition Composition { get; }

    public async ValueTask DisposeAsync()
    {
        MainWindowViewModel.Dispose();
        await extensionHost_.DisposeAsync();
    }
}

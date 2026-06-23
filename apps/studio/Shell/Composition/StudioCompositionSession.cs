using System;
using System.Threading.Tasks;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionSession(
    MainWindowViewModel mainWindowViewModel,
    EditorExtensionComposition composition,
    EditorExtensionHost extensionHost) : IAsyncDisposable
{
    public MainWindowViewModel MainWindowViewModel { get; } = mainWindowViewModel;

    internal EditorExtensionComposition Composition { get; } = composition;

    public ValueTask DisposeAsync()
    {
        return extensionHost.DisposeAsync();
    }
}

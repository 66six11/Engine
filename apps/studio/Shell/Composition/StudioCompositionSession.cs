using System;
using System.Threading.Tasks;
using Editor.Shell.Compatibility;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionSession : IAsyncDisposable
{
    private readonly LegacyEditorModuleCompatibilityAdapter compatibilityAdapter_;

    public StudioCompositionSession(
        MainWindowViewModel mainWindowViewModel,
        EditorExtensionComposition composition,
        LegacyEditorModuleCompatibilityAdapter compatibilityAdapter)
    {
        MainWindowViewModel = mainWindowViewModel;
        Composition = composition;
        compatibilityAdapter_ = compatibilityAdapter;
    }

    public MainWindowViewModel MainWindowViewModel { get; }

    internal EditorExtensionComposition Composition { get; }

    public async ValueTask DisposeAsync()
    {
        MainWindowViewModel.Dispose();
        await compatibilityAdapter_.DisposeAsync();
    }
}

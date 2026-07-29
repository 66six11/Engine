using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Editor.Shell.Compatibility;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionSession : IAsyncDisposable
{
    private readonly LegacyEditorModuleCompatibilityAdapter compatibilityAdapter_;
    private readonly IDisposable? projectSceneProjection_;

    public StudioCompositionSession(
        MainWindowViewModel mainWindowViewModel,
        EditorExtensionComposition composition,
        LegacyEditorModuleCompatibilityAdapter compatibilityAdapter,
        IDisposable? projectSceneProjection = null)
    {
        MainWindowViewModel = mainWindowViewModel;
        Composition = composition;
        compatibilityAdapter_ = compatibilityAdapter;
        projectSceneProjection_ = projectSceneProjection;
    }

    public MainWindowViewModel MainWindowViewModel { get; }

    internal EditorExtensionComposition Composition { get; }

    public async ValueTask DisposeAsync()
    {
        var failures = new List<Exception>();
        try
        {
            MainWindowViewModel.Dispose();
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }

        try
        {
            projectSceneProjection_?.Dispose();
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }

        try
        {
            await compatibilityAdapter_.DisposeAsync();
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }

        if (failures.Count > 0)
        {
            throw new AggregateException(failures);
        }
    }
}

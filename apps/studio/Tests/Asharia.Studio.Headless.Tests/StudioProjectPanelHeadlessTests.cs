using System;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.TestSupport;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.Views.Panels;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioProjectPanelHeadlessTests
{
    private static readonly Guid TestProjectId =
        Guid.Parse("672bf2a1-357e-4b5a-acb0-94cce8de3f02");
    [AvaloniaFact]
    public void Narrow_panel_shows_explicit_no_project_state()
    {
        using var shell = StudioShellTestFactory.Create();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            shell.ProjectAssetCatalog,
            shell.EditorSelection);
        var view = new StudioProjectPanelView { DataContext = viewModel };
        var window = new Window { Width = 260, Height = 320, Content = view };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(260d, view.Bounds.Width);
            Assert.True(Assert.IsType<Border>(
                view.FindControl<Border>("ResourceBlockingState")).IsVisible);
            Assert.False(Assert.IsType<TextBox>(
                view.FindControl<TextBox>("ResourceSearchBox")).IsEnabled);
            Assert.False(Assert.IsType<Border>(
                view.FindControl<Border>("ResourceEmptyState")).IsVisible);
            Assert.True(view.DesiredSize.Width <= 260d);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Dense_catalog_uses_constrained_virtualized_navigation_and_asset_lists()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var gateway = new SequencedAssetCatalogGateway(
            AssetCatalogQueryResult.Success(Snapshot(
                2,
                Enumerable.Range(0, 10_000)
                    .Select(Entry)
                    .ToImmutableArray())));
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            shell.EditorSelection);
        projectSession.Publish(ReadyProject());
        var view = new StudioProjectPanelView { DataContext = viewModel };
        var window = new Window { Width = 260, Height = 360, Content = view };

        try
        {
            window.Show();
            await PumpUntil(() => viewModel.IsReady);

            var navigation = Assert.IsType<ListBox>(
                view.FindControl<ListBox>("ResourceNavigationList"));
            var assets = Assert.IsType<ListBox>(
                view.FindControl<ListBox>("ResourceAssetList"));
            Assert.Single(navigation.GetVisualDescendants()
                .OfType<VirtualizingStackPanel>());
            Assert.Single(assets.GetVisualDescendants()
                .OfType<VirtualizingStackPanel>());
            Assert.Equal(10_000, assets.ItemCount);
            var realizedRows = assets.GetVisualDescendants()
                .OfType<ListBoxItem>()
                .ToArray();
            Assert.NotEmpty(realizedRows);
            Assert.True(realizedRows.Length < assets.ItemCount);
            Assert.All(
                realizedRows,
                row => Assert.InRange(row.Bounds.Height, 21.5d, 22.5d));
            Assert.True(assets.Bounds.Width <= view.Bounds.Width);
            Assert.True(Assert.IsType<TextBox>(
                view.FindControl<TextBox>("ResourceSearchBox")).IsEnabled);

            assets.SelectedIndex = 0;
            Dispatcher.UIThread.RunJobs();
            Assert.True(viewModel.HasSelection);
            Assert.IsType<AssetSelectionTarget>(
                shell.EditorSelection.Current.Primary);
            Assert.Null(view.FindControl<Control>("ResourceDetailsToggle"));
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Degraded_and_empty_states_remain_actionable_without_hiding_last_good()
    {
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        var gateway = new SequencedAssetCatalogGateway(
            AssetCatalogQueryResult.Success(
                Snapshot(1, ImmutableArray<AssetCatalogEntry>.Empty)),
            AssetCatalogQueryResult.Failed(new AssetCatalogQueryFailure(
                AssetCatalogQueryFailureKind.IoFailure,
                "Refresh could not read the manifest.")));
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            shell.EditorSelection);
        projectSession.Publish(ReadyProject());
        var view = new StudioProjectPanelView { DataContext = viewModel };
        var window = new Window { Width = 260, Height = 320, Content = view };

        try
        {
            window.Show();
            await PumpUntil(() => viewModel.IsReady);
            await catalog.RefreshAsync();
            await PumpUntil(() => viewModel.IsDegraded);

            Assert.True(Assert.IsType<Border>(
                view.FindControl<Border>("ResourceDegradedBanner")).IsVisible);
            Assert.False(Assert.IsType<Border>(
                view.FindControl<Border>("ResourceBlockingState")).IsVisible);
            Assert.True(Assert.IsType<Border>(
                view.FindControl<Border>("ResourceEmptyState")).IsVisible);
            Assert.Contains("manifest", viewModel.DegradedMessage);
        }
        finally
        {
            window.Close();
        }
    }

    private static async Task PumpUntil(Func<bool> predicate)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        while (!predicate())
        {
            Dispatcher.UIThread.RunJobs();
            await Task.Delay(5, timeout.Token);
        }
        Dispatcher.UIThread.RunJobs();
    }

    private static AssetCatalogSnapshot Snapshot(
        ulong revision,
        ImmutableArray<AssetCatalogEntry> entries) =>
        new(
            AssetCatalogSnapshotState.Ready,
            revision,
            DateTimeOffset.UtcNow,
            TestProjectId,
            "C:\\Projects\\Sample\\asharia.project.json",
            "C:\\Projects\\Sample\\.asharia\\cache\\assets\\manifest.json",
            "editor-preview",
            [new AssetCatalogSourceRoot(
                "Assets",
                "Assets",
                "Assets",
                "C:\\Projects\\Sample\\Assets")],
            [new AssetCatalogNavigationEntry(
                "root:assets",
                parentKey: null,
                AssetCatalogNavigationKind.SourceRoot,
                "Assets",
                "Assets",
                sourcePath: string.Empty,
                sourceRootName: "Assets",
                sourceRootPrefix: "Assets",
                sourceRootDirectory: "C:\\Projects\\Sample\\Assets",
                assetGuid: null,
                stableId: string.Empty,
                assetTypeName: string.Empty,
                importerName: string.Empty,
                extension: string.Empty,
                importProfileName: string.Empty,
                assetRoleName: string.Empty,
                subAssetCount: 0,
                AssetCatalogProductState.NotTracked,
                depth: 0)],
            entries,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

    private static AssetCatalogEntry Entry(int index)
    {
        var guid = Guid.Parse($"00000000-0000-0000-0000-{index + 1:000000000000}");
        var sourcePath = $"Assets/Texture{index:000}.png";
        return new AssetCatalogEntry(
            new AssetSelectionKey(guid, sourcePath),
            guid,
            guid.ToString("D"),
            sourcePath,
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            $"C:\\Projects\\Sample\\Assets\\Texture{index:000}.png",
            $"C:\\Projects\\Sample\\Assets\\Texture{index:000}.png.ameta",
            $"Texture {index:000}",
            ".png",
            "Texture2D",
            "PngImporter",
            importerVersion: 1,
            "default",
            "Texture",
            AssetCatalogProductState.Current,
            currentProductCount: 1,
            staleProductCount: 0,
            ImmutableArray<AssetCatalogSubAsset>.Empty,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);
    }

    private static ProjectSessionSnapshot ReadyProject()
    {
        var project = new ActiveProjectSnapshot(
            ProjectSessionId.CreateNew(),
            TestProjectId,
            "Sample",
            "C:\\Projects\\Sample");
        return ProjectSessionSnapshot.Ready(
            project,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "C:\\Projects\\Sample\\Assets\\Default.asharia.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
    }

    private sealed class SequencedAssetCatalogGateway(
        params AssetCatalogQueryResult[] results) : IAssetCatalogGateway
    {
        private int nextResult_;

        public ValueTask<AssetCatalogQueryResult> QueryAsync(
            AssetCatalogQueryScope scope,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var index = Interlocked.Increment(ref nextResult_) - 1;
            return ValueTask.FromResult(results[index]);
        }
    }
}

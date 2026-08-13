using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Studio.Application.Assets;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Windowing;
using Editor.UI.Icons;

namespace Editor.Shell.ViewModels.Panels;

internal interface IStudioResourceBrowserUiScheduler
{
    void Post(Action action);

    IDisposable Schedule(Action action, TimeSpan delay);
}

internal sealed class StudioAvaloniaResourceBrowserUiScheduler :
    IStudioResourceBrowserUiScheduler
{
    public static StudioAvaloniaResourceBrowserUiScheduler Instance { get; } = new();

    private StudioAvaloniaResourceBrowserUiScheduler()
    {
    }

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        Dispatcher.UIThread.Post(action, DispatcherPriority.Background);
    }

    public IDisposable Schedule(Action action, TimeSpan delay)
    {
        ArgumentNullException.ThrowIfNull(action);
        return DispatcherTimer.RunOnce(action, delay, DispatcherPriority.Background);
    }
}

internal sealed class StudioResourceBrowserCommand(
    Action execute,
    Func<bool> canExecute) : ICommand
{
    public event EventHandler? CanExecuteChanged;

    public bool CanExecute(object? parameter) => canExecute();

    public void Execute(object? parameter)
    {
        if (CanExecute(parameter))
        {
            execute();
        }
    }

    public void NotifyCanExecuteChanged() =>
        CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}

internal sealed class StudioProjectPanelViewModel :
    StudioDockPanelViewModel,
    System.ComponentModel.INotifyPropertyChanged,
    IDisposable
{
    internal const string AllTypes = "All types";

    private static readonly TimeSpan SearchDebounceInterval =
        TimeSpan.FromMilliseconds(150);
    private static readonly IReadOnlyList<StudioResourceProductFilterOption>
        ProductFilterChoices =
        [
            StudioResourceProductFilterOption.All,
            new(AssetCatalogProductState.Current, "Current"),
            new(AssetCatalogProductState.Missing, "Missing"),
            new(AssetCatalogProductState.Stale, "Stale"),
            new(AssetCatalogProductState.Invalid, "Invalid"),
            new(AssetCatalogProductState.NotTracked, "Not tracked"),
        ];

    private readonly IProjectAssetCatalog assetCatalog_;
    private readonly IStudioResourceBrowserUiScheduler scheduler_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly StudioResourceBrowserCommand refreshCommand_;
    private readonly object searchGate_ = new();
    private IDisposable? pendingSearchRebuild_;
    private AssetCatalogSessionSnapshot snapshot_;
    private AssetCatalogQueryScope? appliedScope_;
    private AssetCatalogSnapshot? projectedCatalog_;
    private IReadOnlyList<StudioResourceAssetRowViewModel> allAssetRows_ = [];
    private IReadOnlyList<StudioResourceNavigationRowViewModel> navigationRows_ = [];
    private IReadOnlyList<StudioResourceAssetRowViewModel> visibleAssets_ = [];
    private IReadOnlyList<string> typeOptions_ = [AllTypes];
    private StudioResourceNavigationRowViewModel? selectedNavigation_;
    private StudioResourceAssetRowViewModel? selectedAsset_;
    private AssetSelectionKey? selectedAssetKey_;
    private string? selectedNavigationKey_;
    private string searchText_ = string.Empty;
    private string selectedType_ = AllTypes;
    private StudioResourceProductFilterOption selectedProductFilter_ =
        StudioResourceProductFilterOption.All;
    private string assetCountText_ = "0";
    private string emptyStateText_ = "No resources found";
    private bool isDetailsExpanded_;
    private bool isReplacingProjection_;
    private ulong appliedGeneration_;
    private int isDisposed_;

    public StudioProjectPanelViewModel(
        StudioShellViewModel shell,
        IProjectAssetCatalog assetCatalog,
        IStudioResourceBrowserUiScheduler? scheduler = null)
        : base(shell)
    {
        ArgumentNullException.ThrowIfNull(assetCatalog);
        assetCatalog_ = assetCatalog;
        scheduler_ = scheduler ?? StudioAvaloniaResourceBrowserUiScheduler.Instance;
        refreshCommand_ = new StudioResourceBrowserCommand(
            RequestRefresh,
            () => CanRefresh);
        assetCatalog_.SnapshotChanged += OnCatalogSnapshotChanged;
        snapshot_ = assetCatalog.Current;
        ApplySnapshot(snapshot_);
    }

    public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<StudioResourceNavigationRowViewModel> NavigationRows =>
        navigationRows_;

    public IReadOnlyList<StudioResourceAssetRowViewModel> VisibleAssets =>
        visibleAssets_;

    public IReadOnlyList<string> TypeOptions => typeOptions_;

    public IReadOnlyList<StudioResourceProductFilterOption> ProductFilterOptions =>
        ProductFilterChoices;

    public string SearchText
    {
        get => searchText_;
        set
        {
            var normalized = value ?? string.Empty;
            if (string.Equals(searchText_, normalized, StringComparison.Ordinal))
            {
                return;
            }

            searchText_ = normalized;
            OnPropertyChanged();
            RequestSearchRebuild();
        }
    }

    public string SelectedType
    {
        get => selectedType_;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? AllTypes : value;
            if (string.Equals(selectedType_, normalized, StringComparison.Ordinal))
            {
                return;
            }

            selectedType_ = normalized;
            OnPropertyChanged();
            RebuildAssetProjection();
        }
    }

    public StudioResourceProductFilterOption SelectedProductFilter
    {
        get => selectedProductFilter_;
        set
        {
            var normalized = value ?? StudioResourceProductFilterOption.All;
            if (selectedProductFilter_ == normalized)
            {
                return;
            }

            selectedProductFilter_ = normalized;
            OnPropertyChanged();
            RebuildAssetProjection();
        }
    }

    public StudioResourceNavigationRowViewModel? SelectedNavigation
    {
        get => selectedNavigation_;
        set
        {
            if (ReferenceEquals(selectedNavigation_, value))
            {
                return;
            }

            if (value is null && isReplacingProjection_
                && selectedNavigationKey_ is not null)
            {
                OnPropertyChanged();
                return;
            }

            selectedNavigation_ = value;
            selectedNavigationKey_ = value?.Key;
            OnPropertyChanged();
            RebuildAssetProjection();
        }
    }

    public StudioResourceAssetRowViewModel? SelectedAsset
    {
        get => selectedAsset_;
        set
        {
            if (ReferenceEquals(selectedAsset_, value))
            {
                return;
            }

            if (value is null && isReplacingProjection_ && selectedAssetKey_ is not null)
            {
                OnPropertyChanged();
                return;
            }

            selectedAsset_ = value;
            selectedAssetKey_ = value?.SelectionKey;
            if (value is null)
            {
                isDetailsExpanded_ = false;
                OnPropertyChanged(nameof(IsDetailsExpanded));
            }
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasSelection));
            OnPropertyChanged(nameof(IsDetailsVisible));
        }
    }

    public bool IsDetailsExpanded
    {
        get => isDetailsExpanded_;
        set
        {
            if (isDetailsExpanded_ == value)
            {
                return;
            }
            isDetailsExpanded_ = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsDetailsVisible));
        }
    }

    public string AssetCountText => assetCountText_;

    public string EmptyStateText => emptyStateText_;

    public bool HasSelection => SelectedAsset is not null;

    public bool IsDetailsVisible => IsDetailsExpanded && HasSelection;

    public bool IsNoProject => snapshot_.State == AssetCatalogSessionState.NoProject;

    public bool IsLoading => snapshot_.State == AssetCatalogSessionState.Loading;

    public bool IsInitialLoading => IsLoading && snapshot_.Catalog is null;

    public bool IsReady => snapshot_.State == AssetCatalogSessionState.Ready;

    public bool IsDegraded => snapshot_.State == AssetCatalogSessionState.Degraded;

    public bool IsFailed => snapshot_.State == AssetCatalogSessionState.Failed;

    public bool IsContentVisible => snapshot_.Catalog is not null && !IsFailed;

    public bool IsBlockingStateVisible => IsNoProject || IsInitialLoading || IsFailed;

    public bool IsSearchEnabled => IsContentVisible;

    public bool IsEmptyStateVisible => IsContentVisible && VisibleAssets.Count == 0;

    public bool CanRefresh =>
        Volatile.Read(ref isDisposed_) == 0
        && snapshot_.Scope is not null
        && !IsLoading;

    public string StatusText => snapshot_.State switch
    {
        AssetCatalogSessionState.NoProject => "No project",
        AssetCatalogSessionState.Loading when snapshot_.Catalog is null => "Loading",
        AssetCatalogSessionState.Loading => $"Refreshing · {AssetCountText}",
        AssetCatalogSessionState.Ready => $"Ready · {AssetCountText}",
        AssetCatalogSessionState.Degraded => $"Degraded · {AssetCountText}",
        AssetCatalogSessionState.Failed => "Unavailable",
        _ => "Unavailable",
    };

    public string BlockingTitle => snapshot_.State switch
    {
        AssetCatalogSessionState.NoProject => "No project open",
        AssetCatalogSessionState.Loading => "Loading resources",
        AssetCatalogSessionState.Failed => "Resource catalog unavailable",
        _ => string.Empty,
    };

    public string BlockingMessage => snapshot_.State switch
    {
        AssetCatalogSessionState.NoProject =>
            "Open a project to browse its source assets and product status.",
        AssetCatalogSessionState.Loading =>
            "Reading the active project's catalog facts.",
        AssetCatalogSessionState.Failed =>
            snapshot_.Failure?.Message ?? "The resource catalog query failed.",
        _ => string.Empty,
    };

    public string DegradedMessage => snapshot_.Failure?.Message
        ?? "The resource catalog is partial. Review its diagnostics for " +
           "unavailable or invalid facts.";

    public string CatalogDiagnosticText
    {
        get
        {
            var count = snapshot_.Catalog?.Diagnostics.Length ?? 0;
            return count == 0
                ? string.Empty
                : $"{count.ToString(CultureInfo.InvariantCulture)} diagnostic(s)";
        }
    }

    public bool HasCatalogDiagnostics =>
        (snapshot_.Catalog?.Diagnostics.Length ?? 0) != 0;

    public ICommand RefreshCommand => refreshCommand_;

    public void Dispose()
    {
        if (Interlocked.Exchange(ref isDisposed_, 1) != 0)
        {
            return;
        }

        assetCatalog_.SnapshotChanged -= OnCatalogSnapshotChanged;
        lifetimeCancellation_.Cancel();
        lock (searchGate_)
        {
            pendingSearchRebuild_?.Dispose();
            pendingSearchRebuild_ = null;
        }
        refreshCommand_.NotifyCanExecuteChanged();
        lifetimeCancellation_.Dispose();
    }

    private void OnCatalogSnapshotChanged(
        object? sender,
        AssetCatalogSessionSnapshotChangedEventArgs e)
    {
        var snapshot = e.Snapshot;
        scheduler_.Post(() =>
        {
            if (Volatile.Read(ref isDisposed_) == 0)
            {
                ApplySnapshot(snapshot);
            }
        });
    }

    private void ApplySnapshot(AssetCatalogSessionSnapshot snapshot)
    {
        if (snapshot.RequestGeneration < appliedGeneration_)
        {
            return;
        }

        var scopeChanged = !SameScope(appliedScope_, snapshot.Scope);
        appliedGeneration_ = snapshot.RequestGeneration;
        appliedScope_ = snapshot.Scope;
        snapshot_ = snapshot;
        if (scopeChanged)
        {
            selectedNavigationKey_ = null;
            selectedAssetKey_ = null;
            selectedAsset_ = null;
            isDetailsExpanded_ = false;
        }

        RebuildCatalogRows();
        RebuildNavigationProjection();
        RebuildTypeOptions();
        RebuildAssetProjection();
        NotifyStateChanged();
    }

    private void RebuildCatalogRows()
    {
        var catalog = snapshot_.Catalog;
        if (ReferenceEquals(projectedCatalog_, catalog))
        {
            return;
        }

        projectedCatalog_ = catalog;
        allAssetRows_ = catalog is null
            ? []
            : catalog.Entries
                .Select(static entry => new StudioResourceAssetRowViewModel(entry))
                .ToArray();
    }

    private void RebuildNavigationProjection()
    {
        var catalog = snapshot_.Catalog;
        if (catalog is null)
        {
            navigationRows_ = [];
            selectedNavigation_ = null;
            OnPropertyChanged(nameof(NavigationRows));
            OnPropertyChanged(nameof(SelectedNavigation));
            return;
        }

        var rows = new List<StudioResourceNavigationRowViewModel>(
            catalog.Navigation.Length + 1)
        {
            StudioResourceNavigationRowViewModel.AllAssets,
        };
        rows.AddRange(catalog.Navigation
            .Where(static entry =>
                entry.Kind is AssetCatalogNavigationKind.SourceRoot
                    or AssetCatalogNavigationKind.Folder)
            .Select(static entry => new StudioResourceNavigationRowViewModel(entry)));
        navigationRows_ = rows.ToArray();
        selectedNavigation_ = selectedNavigationKey_ is null
            ? navigationRows_[0]
            : navigationRows_.FirstOrDefault(row => string.Equals(
                row.Key,
                selectedNavigationKey_,
                StringComparison.Ordinal)) ?? navigationRows_[0];
        selectedNavigationKey_ = selectedNavigation_.Key;
        isReplacingProjection_ = true;
        try
        {
            OnPropertyChanged(nameof(NavigationRows));
            OnPropertyChanged(nameof(SelectedNavigation));
        }
        finally
        {
            isReplacingProjection_ = false;
        }
    }

    private void RebuildTypeOptions()
    {
        var catalogTypes = snapshot_.Catalog is { } catalog
            ? catalog.Entries
                .Select(static entry => entry.AssetTypeName)
                .Where(static name => !string.IsNullOrWhiteSpace(name))
                .Distinct(StringComparer.Ordinal)
                .OrderBy(static name => name, StringComparer.Ordinal)
            : Enumerable.Empty<string>();
        var options = new[] { AllTypes }
            .Concat(catalogTypes)
            .ToArray();
        typeOptions_ = options;
        if (!options.Contains(selectedType_, StringComparer.Ordinal))
        {
            selectedType_ = AllTypes;
            OnPropertyChanged(nameof(SelectedType));
        }
        OnPropertyChanged(nameof(TypeOptions));
    }

    private void RequestSearchRebuild()
    {
        lock (searchGate_)
        {
            pendingSearchRebuild_?.Dispose();
            pendingSearchRebuild_ = scheduler_.Schedule(
                CompleteSearchRebuild,
                SearchDebounceInterval);
        }
    }

    private void CompleteSearchRebuild()
    {
        lock (searchGate_)
        {
            pendingSearchRebuild_ = null;
        }

        if (Volatile.Read(ref isDisposed_) == 0)
        {
            RebuildAssetProjection();
        }
    }

    private void RebuildAssetProjection()
    {
        var entries = allAssetRows_;
        var selectedScope = selectedNavigation_?.ScopePath;
        var isAllScope = selectedNavigation_?.IsAllAssets != false;
        var scopeEntries = entries.Where(row =>
            isAllScope || IsDirectChild(row.SourcePath, selectedScope ?? string.Empty));
        var scopeCount = scopeEntries.Count();
        var query = searchText_.Trim();
        var hasSearch = query.Length != 0;
        var filtered = scopeEntries.Where(row =>
            (string.Equals(selectedType_, AllTypes, StringComparison.Ordinal)
             || string.Equals(row.AssetTypeName, selectedType_, StringComparison.Ordinal))
            && (selectedProductFilter_.State is null
                || row.Entry.ProductState == selectedProductFilter_.State)
            && (!hasSearch || Matches(row.Entry, query)))
            .ToArray();

        var nextSelection = selectedAssetKey_ is { } selectedKey
            ? filtered.FirstOrDefault(row => row.SelectionKey.Equals(selectedKey))
            : null;
        var selectionChanged = !ReferenceEquals(selectedAsset_, nextSelection);
        visibleAssets_ = filtered;
        selectedAsset_ = nextSelection;
        assetCountText_ = filtered.Length == scopeCount
            ? filtered.Length.ToString(CultureInfo.InvariantCulture)
            : $"{filtered.Length.ToString(CultureInfo.InvariantCulture)}/" +
              scopeCount.ToString(CultureInfo.InvariantCulture);
        emptyStateText_ = DetermineEmptyStateText(entries.Count, scopeCount, hasSearch);

        isReplacingProjection_ = true;
        try
        {
            OnPropertyChanged(nameof(VisibleAssets));
            OnPropertyChanged(nameof(AssetCountText));
            OnPropertyChanged(nameof(EmptyStateText));
            OnPropertyChanged(nameof(IsEmptyStateVisible));
            OnPropertyChanged(nameof(StatusText));
            if (selectionChanged)
            {
                OnPropertyChanged(nameof(SelectedAsset));
                OnPropertyChanged(nameof(HasSelection));
                OnPropertyChanged(nameof(IsDetailsVisible));
            }
        }
        finally
        {
            isReplacingProjection_ = false;
        }
    }

    private string DetermineEmptyStateText(
        int totalEntryCount,
        int scopeEntryCount,
        bool hasSearch)
    {
        if (totalEntryCount == 0)
        {
            return "No resources found";
        }
        if (scopeEntryCount == 0)
        {
            return "This folder is empty";
        }
        if (hasSearch
            || !string.Equals(selectedType_, AllTypes, StringComparison.Ordinal)
            || selectedProductFilter_.State is not null)
        {
            return "No matching resources";
        }
        return "No resources found";
    }

    private void RequestRefresh()
    {
        _ = RefreshAsync();
    }

    internal async Task RefreshAsync()
    {
        if (!CanRefresh)
        {
            return;
        }

        try
        {
            await assetCatalog_.RefreshAsync(lifetimeCancellation_.Token);
        }
        catch (OperationCanceledException) when (lifetimeCancellation_.IsCancellationRequested)
        {
        }
    }

    private void NotifyStateChanged()
    {
        OnPropertyChanged(nameof(IsNoProject));
        OnPropertyChanged(nameof(IsLoading));
        OnPropertyChanged(nameof(IsInitialLoading));
        OnPropertyChanged(nameof(IsReady));
        OnPropertyChanged(nameof(IsDegraded));
        OnPropertyChanged(nameof(IsFailed));
        OnPropertyChanged(nameof(IsContentVisible));
        OnPropertyChanged(nameof(IsBlockingStateVisible));
        OnPropertyChanged(nameof(IsSearchEnabled));
        OnPropertyChanged(nameof(IsEmptyStateVisible));
        OnPropertyChanged(nameof(CanRefresh));
        OnPropertyChanged(nameof(StatusText));
        OnPropertyChanged(nameof(BlockingTitle));
        OnPropertyChanged(nameof(BlockingMessage));
        OnPropertyChanged(nameof(DegradedMessage));
        OnPropertyChanged(nameof(CatalogDiagnosticText));
        OnPropertyChanged(nameof(HasCatalogDiagnostics));
        OnPropertyChanged(nameof(IsDetailsExpanded));
        OnPropertyChanged(nameof(HasSelection));
        OnPropertyChanged(nameof(IsDetailsVisible));
        refreshCommand_.NotifyCanExecuteChanged();
    }

    private static bool Matches(AssetCatalogEntry entry, string query) =>
        entry.DisplayName.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.SourcePath.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.AssetTypeName.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.ImporterName.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.ImportProfileName.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.AssetRoleName.Contains(query, StringComparison.CurrentCultureIgnoreCase)
        || entry.GuidText.Contains(query, StringComparison.OrdinalIgnoreCase);

    private static bool IsDirectChild(string sourcePath, string scopePath)
    {
        var normalizedPath = NormalizePath(sourcePath);
        var separator = normalizedPath.LastIndexOf('/');
        var parent = separator < 0 ? string.Empty : normalizedPath[..separator];
        return string.Equals(parent, NormalizePath(scopePath), StringComparison.Ordinal);
    }

    private static string NormalizePath(string path) =>
        (path ?? string.Empty).Replace('\\', '/').Trim('/');

    private static bool SameScope(
        AssetCatalogQueryScope? left,
        AssetCatalogQueryScope? right) =>
        ReferenceEquals(left, right)
        || left is not null
        && right is not null
        && left.SessionId == right.SessionId
        && left.ProjectId == right.ProjectId
        && string.Equals(left.ProjectRootPath, right.ProjectRootPath, StringComparison.Ordinal)
        && string.Equals(left.ProjectFilePath, right.ProjectFilePath, StringComparison.Ordinal)
        && string.Equals(left.TargetProfile, right.TargetProfile, StringComparison.Ordinal);

    private void OnPropertyChanged(
        [System.Runtime.CompilerServices.CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(propertyName));
}

internal sealed class StudioResourceNavigationRowViewModel
{
    private StudioResourceNavigationRowViewModel()
    {
        Key = "@all";
        DisplayName = "All Assets";
        ScopePath = string.Empty;
        IconKey = EditorIconKey.PanelProject;
        IsAllAssets = true;
    }

    public StudioResourceNavigationRowViewModel(AssetCatalogNavigationEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        Key = entry.Key;
        DisplayName = entry.DisplayName;
        ScopePath = entry.ScopePath;
        IconKey = entry.Kind == AssetCatalogNavigationKind.SourceRoot
            ? EditorIconKey.PanelProject
            : EditorIconKey.ObjectDefault;
        IndentWidth = Math.Min(entry.Depth, 8) * 12d;
    }

    public static StudioResourceNavigationRowViewModel AllAssets { get; } = new();

    public string Key { get; }

    public string DisplayName { get; }

    public string ScopePath { get; }

    public string IconKey { get; }

    public double IndentWidth { get; }

    public bool IsAllAssets { get; }

    public string AutomationName => $"{DisplayName}, resource folder";
}

internal sealed class StudioResourceAssetRowViewModel
{
    public StudioResourceAssetRowViewModel(AssetCatalogEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        Entry = entry;
        SelectionKey = entry.SelectionKey;
        DisplayName = entry.DisplayName;
        SourcePath = entry.SourcePath;
        AssetTypeName = entry.AssetTypeName;
        ProductStateText = entry.ProductState switch
        {
            AssetCatalogProductState.NotTracked => "Not tracked",
            AssetCatalogProductState.Current => "Current",
            AssetCatalogProductState.Missing => "Missing",
            AssetCatalogProductState.Stale => "Stale",
            AssetCatalogProductState.Invalid => "Invalid",
            _ => "Unknown",
        };
    }

    public AssetCatalogEntry Entry { get; }

    public AssetSelectionKey SelectionKey { get; }

    public string DisplayName { get; }

    public string SourcePath { get; }

    public string AssetTypeName { get; }

    public string ProductStateText { get; }

    public string GuidText => string.IsNullOrWhiteSpace(Entry.GuidText)
        ? "Untracked"
        : Entry.GuidText;

    public string ImporterText => string.IsNullOrWhiteSpace(Entry.ImporterName)
        ? "No importer"
        : $"{Entry.ImporterName} v" +
          Entry.ImporterVersion.ToString(CultureInfo.InvariantCulture);

    public string ProfileText => string.IsNullOrWhiteSpace(Entry.ImportProfileName)
        ? "Default profile"
        : Entry.ImportProfileName;

    public string RoleText => string.IsNullOrWhiteSpace(Entry.AssetRoleName)
        ? "Unspecified role"
        : Entry.AssetRoleName;

    public string ProductCountText =>
        $"{Entry.CurrentProductCount.ToString(CultureInfo.InvariantCulture)} current · " +
        $"{Entry.StaleProductCount.ToString(CultureInfo.InvariantCulture)} stale";

    public string SubAssetSummaryText => Entry.SubAssets.Length == 0
        ? "None"
        : string.Join(
            ", ",
            Entry.SubAssets.Select(static subAsset =>
                $"{subAsset.DisplayName} [{subAsset.StableId}] · " +
                subAsset.AssetRoleName));

    public string DiagnosticSummaryText => Entry.Diagnostics.Length == 0
        ? "None"
        : string.Join(
            " | ",
            Entry.Diagnostics.Select(static diagnostic =>
                $"{diagnostic.Severity} {diagnostic.Code}: {diagnostic.Message}"));

    public string IconKey => EditorIconKey.ObjectDefault;

    public string DetailHeader => $"{DisplayName} · {ProductStateText}";

    public string AutomationName =>
        $"{DisplayName}, {AssetTypeName}, {ProductStateText}";
}

internal sealed record StudioResourceProductFilterOption(
    AssetCatalogProductState? State,
    string DisplayName)
{
    public static StudioResourceProductFilterOption All { get; } =
        new(null, "All products");
}

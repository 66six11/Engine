using System;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Assets;

public enum AssetCatalogSessionState
{
    NoProject,
    Loading,
    Ready,
    Degraded,
    Failed,
}

public enum AssetCatalogProductState
{
    NotTracked,
    Current,
    Missing,
    Stale,
    Invalid,
}

public enum AssetCatalogSnapshotState
{
    Ready,
    Degraded,
    Failed,
}

public enum AssetCatalogNavigationKind
{
    SourceRoot,
    Folder,
    Asset,
    SubAsset,
}

public enum AssetCatalogDiagnosticSeverity
{
    Info,
    Warning,
    Error,
}

public enum AssetCatalogQueryFailureKind
{
    InvalidInput,
    InvalidProject,
    IoFailure,
    NativeUnavailable,
    LimitExceeded,
    InvalidResponse,
    InternalError,
}

public sealed record AssetCatalogQueryScope
{
    public AssetCatalogQueryScope(
        ProjectSessionId sessionId,
        Guid projectId,
        string projectRootPath,
        string projectFilePath,
        string targetProfile)
    {
        if (!sessionId.IsValid)
        {
            throw new ArgumentException("Asset catalog scope requires a valid project session id.", nameof(sessionId));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException("Asset catalog scope requires a non-empty project id.", nameof(projectId));
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRootPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(projectFilePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetProfile);

        SessionId = sessionId;
        ProjectId = projectId;
        ProjectRootPath = projectRootPath;
        ProjectFilePath = projectFilePath;
        TargetProfile = targetProfile;
    }

    public ProjectSessionId SessionId { get; }
    public Guid ProjectId { get; }
    public string ProjectRootPath { get; }
    public string ProjectFilePath { get; }
    public string TargetProfile { get; }
}

public readonly struct AssetSelectionKey : IEquatable<AssetSelectionKey>
{
    public AssetSelectionKey(Guid? assetGuid, string? sourcePath)
    {
        if (assetGuid == Guid.Empty)
        {
            throw new ArgumentException("Asset selection guid must not be empty.", nameof(assetGuid));
        }
        if (assetGuid is null && string.IsNullOrWhiteSpace(sourcePath))
        {
            throw new ArgumentException(
                "An untracked asset selection requires a source path.",
                nameof(sourcePath));
        }

        AssetGuid = assetGuid;
        SourcePath = sourcePath ?? string.Empty;
    }

    public Guid? AssetGuid { get; }
    public string SourcePath { get; }

    public bool Equals(AssetSelectionKey other) =>
        AssetGuid is { } guid
            ? other.AssetGuid == guid
            : other.AssetGuid is null
              && string.Equals(SourcePath, other.SourcePath, StringComparison.Ordinal);

    public override bool Equals(object? obj) =>
        obj is AssetSelectionKey other && Equals(other);

    public override int GetHashCode() =>
        AssetGuid?.GetHashCode()
        ?? StringComparer.Ordinal.GetHashCode(SourcePath);

    public static bool operator ==(AssetSelectionKey left, AssetSelectionKey right) =>
        left.Equals(right);

    public static bool operator !=(AssetSelectionKey left, AssetSelectionKey right) =>
        !left.Equals(right);
}

public sealed record AssetCatalogNavigationEntry
{
    public AssetCatalogNavigationEntry(
        string key,
        string? parentKey,
        AssetCatalogNavigationKind kind,
        string displayName,
        string scopePath,
        string sourcePath,
        string sourceRootName,
        string sourceRootPrefix,
        string sourceRootDirectory,
        Guid? assetGuid,
        string stableId,
        string assetTypeName,
        string importerName,
        string extension,
        string importProfileName,
        string assetRoleName,
        int subAssetCount,
        AssetCatalogProductState productState,
        int depth)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayName);
        ArgumentNullException.ThrowIfNull(scopePath);
        ArgumentNullException.ThrowIfNull(sourcePath);
        ArgumentNullException.ThrowIfNull(sourceRootName);
        ArgumentNullException.ThrowIfNull(sourceRootPrefix);
        ArgumentNullException.ThrowIfNull(sourceRootDirectory);
        ArgumentNullException.ThrowIfNull(stableId);
        ArgumentNullException.ThrowIfNull(assetTypeName);
        ArgumentNullException.ThrowIfNull(importerName);
        ArgumentNullException.ThrowIfNull(extension);
        ArgumentNullException.ThrowIfNull(importProfileName);
        ArgumentNullException.ThrowIfNull(assetRoleName);
        if (assetGuid == Guid.Empty)
        {
            throw new ArgumentException("Navigation asset guid must not be empty.", nameof(assetGuid));
        }
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (depth < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(depth));
        }
        if (subAssetCount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(subAssetCount));
        }
        if (!Enum.IsDefined(productState))
        {
            throw new ArgumentOutOfRangeException(nameof(productState), productState, null);
        }
        if (kind is AssetCatalogNavigationKind.Asset or AssetCatalogNavigationKind.SubAsset
            && string.IsNullOrWhiteSpace(sourcePath))
        {
            throw new ArgumentException("Asset navigation requires a source path.", nameof(sourcePath));
        }
        if (kind == AssetCatalogNavigationKind.SubAsset
            && string.IsNullOrWhiteSpace(stableId))
        {
            throw new ArgumentException("Sub-asset navigation requires a stable id.", nameof(stableId));
        }

        Key = key;
        ParentKey = parentKey;
        Kind = kind;
        DisplayName = displayName;
        ScopePath = scopePath;
        SourcePath = sourcePath;
        SourceRootName = sourceRootName;
        SourceRootPrefix = sourceRootPrefix;
        SourceRootDirectory = sourceRootDirectory;
        AssetGuid = assetGuid;
        StableId = stableId;
        AssetTypeName = assetTypeName;
        ImporterName = importerName;
        Extension = extension;
        ImportProfileName = importProfileName;
        AssetRoleName = assetRoleName;
        SubAssetCount = subAssetCount;
        ProductState = productState;
        Depth = depth;
    }

    public string Key { get; }
    public string? ParentKey { get; }
    public AssetCatalogNavigationKind Kind { get; }
    public string DisplayName { get; }
    public string ScopePath { get; }
    public string SourcePath { get; }
    public string SourceRootName { get; }
    public string SourceRootPrefix { get; }
    public string SourceRootDirectory { get; }
    public Guid? AssetGuid { get; }
    public string StableId { get; }
    public string AssetTypeName { get; }
    public string ImporterName { get; }
    public string Extension { get; }
    public string ImportProfileName { get; }
    public string AssetRoleName { get; }
    public int SubAssetCount { get; }
    public AssetCatalogProductState ProductState { get; }
    public int Depth { get; }
}

public sealed record AssetCatalogSourceRoot
{
    public AssetCatalogSourceRoot(
        string name,
        string sourcePathPrefix,
        string directory,
        string resolvedDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(sourcePathPrefix);
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        ArgumentException.ThrowIfNullOrWhiteSpace(resolvedDirectory);
        Name = name;
        SourcePathPrefix = sourcePathPrefix;
        Directory = directory;
        ResolvedDirectory = resolvedDirectory;
    }

    public string Name { get; }
    public string SourcePathPrefix { get; }
    public string Directory { get; }
    public string ResolvedDirectory { get; }
}

public sealed record AssetCatalogEntry
{
    public AssetCatalogEntry(
        AssetSelectionKey selectionKey,
        Guid? assetGuid,
        string guidText,
        string sourcePath,
        string sourceRootName,
        string sourceRootPrefix,
        string sourceRootDirectory,
        string sourceFilePath,
        string metadataFilePath,
        string displayName,
        string extension,
        string assetTypeName,
        string importerName,
        ulong importerVersion,
        string importProfileName,
        string assetRoleName,
        AssetCatalogProductState productState,
        int currentProductCount,
        int staleProductCount,
        ImmutableArray<AssetCatalogSubAsset> subAssets,
        ImmutableArray<AssetCatalogDiagnostic> diagnostics)
    {
        if (assetGuid == Guid.Empty)
        {
            throw new ArgumentException("Asset guid must not be empty.", nameof(assetGuid));
        }
        if (selectionKey.AssetGuid != assetGuid)
        {
            throw new ArgumentException("Asset selection identity does not match its catalog entry.", nameof(selectionKey));
        }
        ArgumentNullException.ThrowIfNull(guidText);
        if (assetGuid is { } guid
            && (!Guid.TryParseExact(guidText, "D", out var parsedGuid)
                || parsedGuid != guid))
        {
            throw new ArgumentException("Asset guid text does not match its typed guid.", nameof(guidText));
        }
        if (assetGuid is null && guidText.Length != 0)
        {
            throw new ArgumentException("An untracked asset cannot contain guid text.", nameof(guidText));
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        ArgumentNullException.ThrowIfNull(sourceRootName);
        ArgumentNullException.ThrowIfNull(sourceRootPrefix);
        ArgumentNullException.ThrowIfNull(sourceRootDirectory);
        ArgumentNullException.ThrowIfNull(sourceFilePath);
        ArgumentNullException.ThrowIfNull(metadataFilePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayName);
        ArgumentNullException.ThrowIfNull(extension);
        ArgumentException.ThrowIfNullOrWhiteSpace(assetTypeName);
        ArgumentNullException.ThrowIfNull(importerName);
        ArgumentNullException.ThrowIfNull(importProfileName);
        ArgumentNullException.ThrowIfNull(assetRoleName);
        if (!Enum.IsDefined(productState))
        {
            throw new ArgumentOutOfRangeException(nameof(productState), productState, null);
        }
        if (currentProductCount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(currentProductCount));
        }
        if (staleProductCount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(staleProductCount));
        }
        if (subAssets.IsDefault)
        {
            throw new ArgumentException("Asset sub-assets must be initialized.", nameof(subAssets));
        }
        if (diagnostics.IsDefault)
        {
            throw new ArgumentException("Asset diagnostics must be initialized.", nameof(diagnostics));
        }
        if (subAssets.Select(item => item.StableId).Distinct(StringComparer.Ordinal).Count()
            != subAssets.Length)
        {
            throw new ArgumentException("Asset sub-asset stable ids must be unique.", nameof(subAssets));
        }

        SelectionKey = selectionKey;
        AssetGuid = assetGuid;
        GuidText = guidText;
        SourcePath = sourcePath;
        SourceRootName = sourceRootName;
        SourceRootPrefix = sourceRootPrefix;
        SourceRootDirectory = sourceRootDirectory;
        SourceFilePath = sourceFilePath;
        MetadataFilePath = metadataFilePath;
        DisplayName = displayName;
        Extension = extension;
        AssetTypeName = assetTypeName;
        ImporterName = importerName;
        ImporterVersion = importerVersion;
        ImportProfileName = importProfileName;
        AssetRoleName = assetRoleName;
        ProductState = productState;
        CurrentProductCount = currentProductCount;
        StaleProductCount = staleProductCount;
        SubAssets = subAssets;
        Diagnostics = diagnostics;
    }

    public AssetSelectionKey SelectionKey { get; }
    public Guid? AssetGuid { get; }
    public string GuidText { get; }
    public string SourcePath { get; }
    public string SourceRootName { get; }
    public string SourceRootPrefix { get; }
    public string SourceRootDirectory { get; }
    public string SourceFilePath { get; }
    public string MetadataFilePath { get; }
    public string DisplayName { get; }
    public string Extension { get; }
    public string AssetTypeName { get; }
    public string ImporterName { get; }
    public ulong ImporterVersion { get; }
    public string ImportProfileName { get; }
    public string AssetRoleName { get; }
    public AssetCatalogProductState ProductState { get; }
    public int CurrentProductCount { get; }
    public int StaleProductCount { get; }
    public ImmutableArray<AssetCatalogSubAsset> SubAssets { get; }
    public ImmutableArray<AssetCatalogDiagnostic> Diagnostics { get; }
}

public sealed record AssetCatalogSubAsset
{
    public AssetCatalogSubAsset(
        string stableId,
        string displayName,
        string assetRoleName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(stableId);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayName);
        ArgumentException.ThrowIfNullOrWhiteSpace(assetRoleName);
        StableId = stableId;
        DisplayName = displayName;
        AssetRoleName = assetRoleName;
    }

    public string StableId { get; }
    public string DisplayName { get; }
    public string AssetRoleName { get; }
}

public sealed record AssetCatalogDiagnostic
{
    public AssetCatalogDiagnostic(
        AssetCatalogDiagnosticSeverity severity,
        string code,
        string? sourcePath,
        string? path,
        string message)
    {
        if (!Enum.IsDefined(severity))
        {
            throw new ArgumentOutOfRangeException(nameof(severity), severity, null);
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);

        Severity = severity;
        Code = code;
        SourcePath = sourcePath;
        Path = path;
        Message = message;
    }

    public AssetCatalogDiagnosticSeverity Severity { get; }
    public string Code { get; }
    public string? SourcePath { get; }
    public string? Path { get; }
    public string Message { get; }
}

public sealed record AssetCatalogSnapshot
{
    public AssetCatalogSnapshot(
        AssetCatalogSnapshotState state,
        ulong revision,
        DateTimeOffset capturedAtUtc,
        Guid projectId,
        string projectFile,
        string productManifestFile,
        string targetProfile,
        ImmutableArray<AssetCatalogSourceRoot> sourceRoots,
        ImmutableArray<AssetCatalogNavigationEntry> navigation,
        ImmutableArray<AssetCatalogEntry> entries,
        ImmutableArray<AssetCatalogDiagnostic> diagnostics)
    {
        if (!Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(nameof(state), state, null);
        }
        if (capturedAtUtc == default)
        {
            throw new ArgumentException("Asset catalog capture time must be set.", nameof(capturedAtUtc));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException("Asset catalog snapshot requires a non-empty project id.", nameof(projectId));
        }
        if (navigation.IsDefault || entries.IsDefault || diagnostics.IsDefault)
        {
            throw new ArgumentException("Asset catalog collections must be initialized.");
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(projectFile);
        ArgumentNullException.ThrowIfNull(productManifestFile);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetProfile);
        if (sourceRoots.IsDefault)
        {
            throw new ArgumentException("Asset source roots must be initialized.", nameof(sourceRoots));
        }

        State = state;
        Revision = revision;
        CapturedAtUtc = capturedAtUtc;
        ProjectId = projectId;
        ProjectFile = projectFile;
        ProductManifestFile = productManifestFile;
        TargetProfile = targetProfile;
        SourceRoots = sourceRoots;
        Navigation = navigation;
        Entries = entries;
        Diagnostics = diagnostics;
    }

    public AssetCatalogSnapshotState State { get; }
    public ulong Revision { get; }
    public DateTimeOffset CapturedAtUtc { get; }
    public Guid ProjectId { get; }
    public string ProjectFile { get; }
    public string ProductManifestFile { get; }
    public string TargetProfile { get; }
    public ImmutableArray<AssetCatalogSourceRoot> SourceRoots { get; }
    public ImmutableArray<AssetCatalogNavigationEntry> Navigation { get; }
    public ImmutableArray<AssetCatalogEntry> Entries { get; }
    public ImmutableArray<AssetCatalogDiagnostic> Diagnostics { get; }
}

public sealed record AssetCatalogQueryFailure
{
    public AssetCatalogQueryFailure(AssetCatalogQueryFailureKind kind, string message)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        Kind = kind;
        Message = message;
    }

    public AssetCatalogQueryFailureKind Kind { get; }
    public string Message { get; }
}

public sealed record AssetCatalogQueryResult
{
    private AssetCatalogQueryResult(
        AssetCatalogSnapshot? snapshot,
        AssetCatalogQueryFailure? failure)
    {
        if ((snapshot is null) == (failure is null))
        {
            throw new ArgumentException(
                "An asset catalog query result requires exactly one snapshot or failure.");
        }
        Snapshot = snapshot;
        Failure = failure;
    }

    public AssetCatalogSnapshot? Snapshot { get; }
    public AssetCatalogQueryFailure? Failure { get; }
    public bool Succeeded => Snapshot is not null;

    public static AssetCatalogQueryResult Success(AssetCatalogSnapshot snapshot) =>
        new(snapshot ?? throw new ArgumentNullException(nameof(snapshot)), failure: null);

    public static AssetCatalogQueryResult Failed(AssetCatalogQueryFailure failure) =>
        new(snapshot: null, failure ?? throw new ArgumentNullException(nameof(failure)));
}

public interface IAssetCatalogGateway
{
    ValueTask<AssetCatalogQueryResult> QueryAsync(
        AssetCatalogQueryScope scope,
        CancellationToken cancellationToken = default);
}

public sealed record AssetCatalogSessionSnapshot
{
    private AssetCatalogSessionSnapshot(
        AssetCatalogSessionState state,
        AssetCatalogQueryScope? scope,
        AssetCatalogSnapshot? catalog,
        AssetCatalogQueryFailure? failure,
        ulong requestGeneration)
    {
        State = state;
        Scope = scope;
        Catalog = catalog;
        Failure = failure;
        RequestGeneration = requestGeneration;
    }

    public static AssetCatalogSessionSnapshot NoProject(ulong requestGeneration = 0) =>
        new(AssetCatalogSessionState.NoProject, null, null, null, requestGeneration);

    internal static AssetCatalogSessionSnapshot Loading(
        AssetCatalogQueryScope scope,
        AssetCatalogSnapshot? lastGood,
        ulong requestGeneration) =>
        new(AssetCatalogSessionState.Loading, scope, lastGood, null, requestGeneration);

    internal static AssetCatalogSessionSnapshot Ready(
        AssetCatalogQueryScope scope,
        AssetCatalogSnapshot catalog,
        ulong requestGeneration) =>
        new(AssetCatalogSessionState.Ready, scope, catalog, null, requestGeneration);

    internal static AssetCatalogSessionSnapshot Degraded(
        AssetCatalogQueryScope scope,
        AssetCatalogSnapshot lastGood,
        AssetCatalogQueryFailure? failure,
        ulong requestGeneration) =>
        new(AssetCatalogSessionState.Degraded, scope, lastGood, failure, requestGeneration);

    internal static AssetCatalogSessionSnapshot Failed(
        AssetCatalogQueryScope scope,
        AssetCatalogQueryFailure failure,
        ulong requestGeneration) =>
        new(AssetCatalogSessionState.Failed, scope, null, failure, requestGeneration);

    public AssetCatalogSessionState State { get; }
    public AssetCatalogQueryScope? Scope { get; }
    public AssetCatalogSnapshot? Catalog { get; }
    public AssetCatalogQueryFailure? Failure { get; }
    public ulong RequestGeneration { get; }
}

public sealed class AssetCatalogSessionSnapshotChangedEventArgs(
    AssetCatalogSessionSnapshot snapshot) : EventArgs
{
    public AssetCatalogSessionSnapshot Snapshot { get; } =
        snapshot ?? throw new ArgumentNullException(nameof(snapshot));
}

public interface IProjectAssetCatalog : IAsyncDisposable
{
    event EventHandler<AssetCatalogSessionSnapshotChangedEventArgs>? SnapshotChanged;

    AssetCatalogSessionSnapshot Current { get; }

    ValueTask RefreshAsync(CancellationToken cancellationToken = default);
}

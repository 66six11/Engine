#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/core/result.hpp"
#include "asharia/project/project_descriptor.hpp"

namespace asharia::editor {

    enum class EditorAssetCatalogDiagnosticSeverity : std::uint8_t {
        Info,
        Warning,
        Error,
    };

    enum class EditorAssetCatalogDiagnosticCode : std::uint8_t {
        InvalidRequest,
        ProjectDescriptorReadFailed,
        ProductManifestReadFailed,
        SourceScan,
        SourceDiscovery,
        SourceSnapshot,
        ImportPlanning,
        CatalogMerge,
        CatalogView,
        LimitExceeded,
    };

    struct EditorAssetCatalogDiagnostic {
        EditorAssetCatalogDiagnosticCode code{EditorAssetCatalogDiagnosticCode::InvalidRequest};
        EditorAssetCatalogDiagnosticSeverity severity{EditorAssetCatalogDiagnosticSeverity::Info};
        std::string sourcePath;
        std::filesystem::path path;
        std::string message;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogDiagnostic&,
                                             const EditorAssetCatalogDiagnostic&) = default;
    };

    struct EditorAssetCatalogSnapshotRequest {
        std::filesystem::path projectFile;
        std::filesystem::path productManifestFile;
        std::string targetProfile{"editor-preview"};
        std::uint64_t maxSourceRoots{1'024U};
        std::uint64_t maxSourceFiles{10'000U};
        std::uint64_t maxTotalSourceBytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
        std::uint64_t maxDiagnostics{10'000U};
        // Host-supplied fingerprints; catalog queries never probe compiler executables.
        std::vector<asharia::asset::AssetImportToolVersionDependency> toolVersions;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogSnapshotRequest&,
                                             const EditorAssetCatalogSnapshotRequest&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !projectFile.empty() && !targetProfile.empty() && maxSourceRoots > 0U &&
                   maxSourceFiles > 0U && maxTotalSourceBytes > 0U && maxDiagnostics > 0U;
        }
    };

    struct EditorAssetCatalogSnapshot {
        std::filesystem::path projectFile;
        std::filesystem::path productManifestFile;
        std::string targetProfile;
        asharia::project::AshariaProjectDescriptor project;
        asharia::asset::AssetCatalogView catalogView;
        std::vector<EditorAssetCatalogDiagnostic> diagnostics;
        // Native selection facts from the same scan/plan as catalogView; not serialized to UI JSON.
        std::vector<asharia::asset::AssetProductKey> expectedProductKeys;
        std::vector<asharia::asset::AssetProductRecord> products;
        // Preserve query identity when reconstructing a native refresh request.
        std::vector<asharia::asset::AssetImportToolVersionDependency> toolVersions;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogSnapshot&,
                                             const EditorAssetCatalogSnapshot&) = default;
        [[nodiscard]] bool succeeded() const noexcept;
    };

    enum class EditorAssetProductSelectionError : int {
        InvalidRequest = 1,
        IncompleteSnapshot,
        AssetNotFound,
        TypeMismatch,
        ExpectedProductUnavailable,
        ProductUnavailable,
        AmbiguousProduct,
        InvalidProduct,
    };

    // Returns an owning record. Does no IO, import, artifact verification or runtime publication.
    [[nodiscard]] Result<asharia::asset::AssetProductRecord>
    selectEditorAssetProduct(const EditorAssetCatalogSnapshot& snapshot,
                             asharia::asset::AssetGuid guid,
                             asharia::asset::AssetTypeId expectedType);

    struct EditorAssetCatalogResolvedSourceRoot {
        bool matched{false};
        std::string rootName;
        std::string sourcePathPrefix;
        std::filesystem::path directory;
        std::filesystem::path resolvedDirectory;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogResolvedSourceRoot&,
                                             const EditorAssetCatalogResolvedSourceRoot&) = default;
    };

    enum class EditorAssetCatalogNavigationNodeKind : std::uint8_t {
        SourceRoot,
        Folder,
        Asset,
        SubAsset,
    };

    struct EditorAssetCatalogNavigationNode {
        EditorAssetCatalogNavigationNodeKind kind{EditorAssetCatalogNavigationNodeKind::Asset};
        std::string key;
        std::string parentKey;
        std::string displayName;
        std::string scopePath;
        std::string sourcePath;
        std::string sourceRootName;
        std::string sourceRootPrefix;
        std::filesystem::path sourceRootDirectory;
        std::string guidText;
        std::string stableId;
        std::string assetTypeName;
        std::string importerName;
        std::string extension;
        std::string importProfileName;
        std::string assetRoleName;
        std::size_t subAssetCount{};
        asharia::asset::AssetCatalogProductState productState{
            asharia::asset::AssetCatalogProductState::NotTracked};

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogNavigationNode&,
                                             const EditorAssetCatalogNavigationNode&) = default;
    };

    [[nodiscard]] EditorAssetCatalogSnapshot
    loadEditorAssetCatalogSnapshot(const EditorAssetCatalogSnapshotRequest& request);
    [[nodiscard]] EditorAssetCatalogSnapshotRequest
    makeEditorAssetCatalogSnapshotRequest(const EditorAssetCatalogSnapshot& snapshot);
    [[nodiscard]] std::filesystem::path
    resolveEditorAssetCatalogSourceFilePath(const EditorAssetCatalogSnapshot& snapshot,
                                            std::string_view sourcePath);
    [[nodiscard]] std::filesystem::path
    resolveEditorAssetCatalogMetadataFilePath(const EditorAssetCatalogSnapshot& snapshot,
                                              std::string_view sourcePath);
    [[nodiscard]] std::vector<EditorAssetCatalogResolvedSourceRoot>
    resolveEditorAssetCatalogSourceRoots(const EditorAssetCatalogSnapshot& snapshot);
    [[nodiscard]] EditorAssetCatalogResolvedSourceRoot
    resolveEditorAssetCatalogSourceRootForSourcePath(const EditorAssetCatalogSnapshot& snapshot,
                                                     std::string_view sourcePath);
    [[nodiscard]] std::vector<EditorAssetCatalogNavigationNode>
    makeEditorAssetCatalogNavigationNodes(const EditorAssetCatalogSnapshot& snapshot);
    [[nodiscard]] std::string_view
    editorAssetCatalogNavigationNodeKindName(EditorAssetCatalogNavigationNodeKind kind) noexcept;
    [[nodiscard]] std::string_view
    editorAssetCatalogDiagnosticCodeName(EditorAssetCatalogDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view editorAssetCatalogDiagnosticSeverityName(
        EditorAssetCatalogDiagnosticSeverity severity) noexcept;

} // namespace asharia::editor

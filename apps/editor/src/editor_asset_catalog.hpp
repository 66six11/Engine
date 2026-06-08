#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_catalog_view.hpp"
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

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogSnapshotRequest&,
                                             const EditorAssetCatalogSnapshotRequest&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !projectFile.empty() && !targetProfile.empty();
        }
    };

    struct EditorAssetCatalogSnapshot {
        std::filesystem::path projectFile;
        std::filesystem::path productManifestFile;
        std::string targetProfile;
        asharia::project::AshariaProjectDescriptor project;
        asharia::asset::AssetCatalogView catalogView;
        std::vector<EditorAssetCatalogDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogSnapshot&,
                                             const EditorAssetCatalogSnapshot&) = default;
        [[nodiscard]] bool succeeded() const noexcept;
    };

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

    class EditorAssetCatalogStore {
    public:
        EditorAssetCatalogStore();

        void useFixtureCatalog();
        void useSnapshot(EditorAssetCatalogSnapshot snapshot);

        [[nodiscard]] const asharia::asset::AssetCatalogView& catalogView() const noexcept;
        [[nodiscard]] const EditorAssetCatalogSnapshot* snapshot() const noexcept;
        [[nodiscard]] std::span<const EditorAssetCatalogDiagnostic> diagnostics() const noexcept;

    private:
        asharia::asset::AssetCatalogView fixtureCatalog_;
        EditorAssetCatalogSnapshot snapshot_;
        bool hasSnapshot_{false};
    };

    [[nodiscard]] EditorAssetCatalogSnapshot
    loadEditorAssetCatalogSnapshot(const EditorAssetCatalogSnapshotRequest& request);
    [[nodiscard]] EditorAssetCatalogSnapshotRequest
    makeEditorAssetCatalogSnapshotRequest(const EditorAssetCatalogSnapshot& snapshot);
    [[nodiscard]] const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store);
    [[nodiscard]] const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store,
                                   const EditorAssetCatalogSnapshotRequest& request);
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
    [[nodiscard]] asharia::asset::AssetCatalogView makeEditorAssetBrowserFixtureCatalogView();
    [[nodiscard]] std::string_view
    editorAssetCatalogDiagnosticCodeName(EditorAssetCatalogDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view editorAssetCatalogDiagnosticSeverityName(
        EditorAssetCatalogDiagnosticSeverity severity) noexcept;

} // namespace asharia::editor

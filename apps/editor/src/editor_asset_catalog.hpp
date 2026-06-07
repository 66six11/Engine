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
        asharia::project::AshariaProjectDescriptor project;
        asharia::asset::AssetCatalogView catalogView;
        std::vector<EditorAssetCatalogDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const EditorAssetCatalogSnapshot&,
                                             const EditorAssetCatalogSnapshot&) = default;
        [[nodiscard]] bool succeeded() const noexcept;
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
    [[nodiscard]] asharia::asset::AssetCatalogView makeEditorAssetBrowserFixtureCatalogView();
    [[nodiscard]] std::string_view
    editorAssetCatalogDiagnosticCodeName(EditorAssetCatalogDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view
    editorAssetCatalogDiagnosticSeverityName(EditorAssetCatalogDiagnosticSeverity severity) noexcept;

} // namespace asharia::editor

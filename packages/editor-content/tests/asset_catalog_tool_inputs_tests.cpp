#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/editor_content/asset_catalog_snapshot.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace {
    struct Workspace {
        std::filesystem::path root;
        explicit Workspace(std::filesystem::path path) : root(std::move(path)) {}
        Workspace(const Workspace&) = delete;
        Workspace& operator=(const Workspace&) = delete;
        Workspace(Workspace&&) = delete;
        Workspace& operator=(Workspace&&) = delete;
        ~Workspace() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    bool prepare(const std::filesystem::path& root) {
        using namespace asharia;
        std::filesystem::create_directories(root / "Assets");
        const auto projectId = project::parseProjectId("11111111-2222-3333-4444-555555555555");
        const auto guid = asset::parseAssetGuid("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        if (!projectId || !guid ||
            !project::writeAshariaProjectDescriptorFile(
                root / "asharia.project.json",
                {.projectName = "ToolInputs",
                 .projectId = *projectId,
                 .assetSourceRoots =
                     {{.rootName = "Assets", .directory = "Assets", .sourcePathPrefix = "Assets"}},
                 .assetCacheRoot = ".asharia/cache/assets"})) {
            return false;
        }
        const auto sourceFile = root / "Assets/test.shader";
        std::ofstream{sourceFile} << "// Metadata planning fixture; no compile is claimed.\n";
        const std::array entries{asset::AssetSourceSnapshotEntry{.sourcePath = "Assets/test.shader",
                                                                 .sourceFilePath = sourceFile}};
        const auto hashed = asset::snapshotAssetSourceFiles(entries);
        if (!hashed.succeeded() || hashed.snapshots.size() != 1U) {
            return false;
        }
        constexpr auto importer = "com.asharia.importer.shader-compile-reflection";
        return asset::writeAssetMetadataFile(
                   root / "Assets/test.shader.ameta",
                   {.source = {.guid = *guid,
                               .assetType = asset::makeAssetTypeId("com.asharia.asset.Shader"),
                               .assetTypeName = "com.asharia.asset.Shader",
                               .sourcePath = "Assets/test.shader",
                               .importerId = asset::makeImporterId(importer),
                               .importerName = importer,
                               .importerVersion = {1U},
                               .sourceHash = hashed.snapshots.front().sourceHash,
                               .settingsHash = asset::hashAssetImportSettings({})}})
            .has_value();
    }

    bool invalidInputs(asharia::editor::EditorAssetCatalogSnapshotRequest request) {
        using namespace asharia::editor;
        request.projectFile = "does-not-exist/asharia.project.json";
        auto rejects = [](const EditorAssetCatalogSnapshotRequest& candidate) {
            const auto result = loadEditorAssetCatalogSnapshot(candidate);
            return !result.succeeded() && result.diagnostics.size() == 1U &&
                   result.diagnostics.front().code ==
                       EditorAssetCatalogDiagnosticCode::InvalidRequest;
        };
        auto invalid = request;
        invalid.toolVersions.front().versionHash = 0U;
        if (!rejects(invalid)) {
            return false;
        }
        invalid = request;
        invalid.toolVersions.push_back(invalid.toolVersions.front());
        if (!rejects(invalid)) {
            return false;
        }
        invalid = request;
        invalid.toolVersions.front().importerId = {};
        if (!rejects(invalid)) {
            return false;
        }
        invalid = request;
        invalid.toolVersions.front().toolName.clear();
        if (!rejects(invalid)) {
            return false;
        }
        invalid.toolVersions.front().toolName = std::string(129U, 'x');
        if (!rejects(invalid)) {
            return false;
        }
        invalid = request;
        invalid.toolVersions.resize(257U);
        return rejects(invalid);
    }

    bool run(const std::filesystem::path& root) {
        using namespace asharia;
        using namespace asharia::editor;
        EditorAssetCatalogSnapshotRequest request{.projectFile = root / "asharia.project.json"};
        const auto absent = loadEditorAssetCatalogSnapshot(request);
        if (!absent.succeeded() || !absent.expectedProductKeys.empty() ||
            !std::ranges::any_of(absent.diagnostics, [](const auto& diagnostic) {
                return diagnostic.message.contains("No environment tools were resolved");
            })) {
            return false;
        }
        const auto importer =
            asset::makeImporterId("com.asharia.importer.shader-compile-reflection");
        request.toolVersions = {{.importerId = importer, .toolName = "slangc", .versionHash = 11U}};
        if (!loadEditorAssetCatalogSnapshot(request).expectedProductKeys.empty()) {
            return false;
        }
        request.toolVersions.push_back(
            {.importerId = importer, .toolName = "spirv-val", .versionHash = 22U});
        const auto declared = loadEditorAssetCatalogSnapshot(request);
        if (!declared.succeeded() || declared.expectedProductKeys.size() != 1U ||
            makeEditorAssetCatalogSnapshotRequest(declared).toolVersions != request.toolVersions ||
            loadEditorAssetCatalogSnapshot(makeEditorAssetCatalogSnapshotRequest(declared)) !=
                declared) {
            return false;
        }
        std::ranges::reverse(request.toolVersions);
        if (loadEditorAssetCatalogSnapshot(request).expectedProductKeys !=
            declared.expectedProductKeys) {
            return false;
        }
        request.toolVersions.front().versionHash++;
        const auto changed = loadEditorAssetCatalogSnapshot(request);
        if (!changed.succeeded() || changed.expectedProductKeys.size() != 1U ||
            changed.expectedProductKeys == declared.expectedProductKeys) {
            return false;
        }
        return invalidInputs(request);
    }
} // namespace

int main() {
    try {
        Workspace workspace{
            std::filesystem::temp_directory_path() /
            ("catalog-tool-inputs-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
        if (prepare(workspace.root) && run(workspace.root)) {
            return 0;
        }
        std::cerr << "Catalog tool input contract failed.\n";
    } catch (...) {
        return 1;
    }
    return 1;
}

#pragma once

#include <string>

#include "asharia/core/result.hpp"
#include "asharia/editor_content/asset_catalog_snapshot.hpp"

namespace asharia::editor {

    class EditorAssetIconRegistry;

    [[nodiscard]] std::string
    writeEditorAssetCatalogSnapshotTextReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot);

    [[nodiscard]] Result<std::string>
    writeEditorAssetCatalogSnapshotJsonReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot);
    [[nodiscard]] Result<std::string>
    writeEditorAssetCatalogSnapshotJsonReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot,
                                              const EditorAssetIconRegistry& iconRegistry);

} // namespace asharia::editor

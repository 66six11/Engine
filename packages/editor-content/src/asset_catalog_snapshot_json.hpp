#pragma once

#include <cstddef>
#include <string>

#include "asharia/core/result.hpp"
#include "asharia/editor_content/asset_catalog_snapshot.hpp"

namespace asharia::editor {

    [[nodiscard]] Result<std::string>
    writeEditorAssetCatalogSnapshotJson(const EditorAssetCatalogSnapshot& snapshot,
                                        std::size_t maxUtf8Bytes, std::size_t maxResponseBytes);
} // namespace asharia::editor

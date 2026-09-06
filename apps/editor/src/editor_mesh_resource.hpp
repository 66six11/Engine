#pragma once

#include "asharia/editor_content/asset_catalog_snapshot.hpp"
#include "asharia/resource_runtime/mesh_resource_store.hpp"

namespace asharia::editor {
    // Owner-thread-only. Selects and queues; caller runs the returned load plan off the frame path
    // and publishes its completion on the store owner thread. Never scans, cooks or reads files.
    [[nodiscard]] Result<resource::MeshResourceRequestResult>
    requestEditorMeshResource(const EditorAssetCatalogSnapshot& snapshot, asset::AssetGuid guid,
                              resource::MeshResourceStore& store);
    [[nodiscard]] bool runEditorMeshResourceSmoke(bool withSharedGpu = false);
} // namespace asharia::editor

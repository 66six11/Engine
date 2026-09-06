#include "editor_mesh_resource.hpp"

#include <array>

namespace asharia::editor {
    Result<resource::MeshResourceRequestResult>
    requestEditorMeshResource(const EditorAssetCatalogSnapshot& snapshot, asset::AssetGuid guid,
                              resource::MeshResourceStore& store) {
        const auto type = asset::makeAssetTypeId(mesh::kMeshAssetTypeName);
        auto product = selectEditorAssetProduct(snapshot, guid, type);
        if (!product) {
            return std::unexpected{std::move(product.error())};
        }
        const std::array records{std::move(*product)};
        return store.request({.guid = guid, .assetType = type}, records.front().key, records);
    }
} // namespace asharia::editor

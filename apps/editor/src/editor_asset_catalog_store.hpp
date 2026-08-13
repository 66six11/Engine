#pragma once

#include <span>

#include "asharia/editor_content/asset_catalog_snapshot.hpp"

namespace asharia::editor {

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

    [[nodiscard]] const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store);
    [[nodiscard]] const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store,
                                   const EditorAssetCatalogSnapshotRequest& request);

} // namespace asharia::editor

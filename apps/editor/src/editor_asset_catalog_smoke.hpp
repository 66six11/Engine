#pragma once

#include "editor_app.hpp"

namespace asharia::editor {

    struct EditorAssetCatalogSnapshot;

    [[nodiscard]] EditorAssetCatalogSnapshot loadEditorAssetCatalogSmokeSnapshot();
    [[nodiscard]] bool validateEditorAssetCatalogSnapshotSmoke(EditorRunMode mode);

} // namespace asharia::editor

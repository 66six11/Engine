#pragma once

#include <array>

#include "editor_panel.hpp"

namespace asharia::editor {

    class AssetBrowserPanel final : public ImGuiAssetBrowserEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;

    private:
        void drawAssetBrowserPanel(EditorAssetBrowserPanelDrawContext& context,
                                   EditorPanelState& state) override;

        EditorPanelDesc desc_{
            .id = EditorId{.value = "asset-browser"},
            .title = "Asset Browser",
            .titleKey = "panel.assetBrowser",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Tools,
            .preferredDock = EditorDockSlot::RightBottom,
        };
        std::array<char, 96> filter_{};
    };

} // namespace asharia::editor

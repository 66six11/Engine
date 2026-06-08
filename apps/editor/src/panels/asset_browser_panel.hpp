#pragma once

#include <array>
#include <string>

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
            .title = "Project",
            .titleKey = "panel.assetBrowser",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Tools,
            .preferredDock = EditorDockSlot::Bottom,
        };
        std::array<char, 96> filter_{};
        std::string selectedFolderScope_;
        std::string selectedAssetTypeFilter_;
        std::string selectedImportProfileFilter_;
        std::string selectedProductStateFilter_;
        std::string selectedAssetKey_;
        std::string selectedNavigationKey_;
        std::string selectedSubAssetStableId_;
        std::string importSettingsSelectionKey_;
        std::string textureProfileDraft_;
        std::string textureProfileBaseline_;
        std::string importSettingsMessage_;
        bool importSettingsMessageIsError_{false};
    };

} // namespace asharia::editor

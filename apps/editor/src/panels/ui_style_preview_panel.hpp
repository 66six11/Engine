#pragma once

#include <array>
#include <cstddef>

#include "editor_panel.hpp"

namespace asharia::editor {

    class UiStylePreviewPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorFrameContext& context, EditorPanelState& state) override;
        void draw(EditorFrameContext& context, EditorPanelState& state) override;

    private:
        EditorPanelDesc desc_{
            .id = EditorId{.value = "ui-style-preview"},
            .title = "UI Style Preview",
            .titleKey = "panel.uiStylePreview",
            .defaultOpen = false,
            .singleton = true,
        };
        std::array<char, 64> textBuffer_{{'A', 's', 'h', 'a', 'r', 'i', 'a', '\0'}};
        bool checkboxValue_{true};
        float sliderValue_{0.42F};
        int comboIndex_{0};
        std::size_t selectedTokenIndex_{0};
    };

} // namespace asharia::editor

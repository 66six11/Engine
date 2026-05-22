#include "panels/scene_view_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"

namespace {

    asharia::editor::EditorExtent2D viewportExtentFromAvailableSize(ImVec2 available) {
        return asharia::editor::EditorExtent2D{
            .width = std::max(1U, static_cast<std::uint32_t>(std::max(available.x, 1.0F))),
            .height = std::max(1U, static_cast<std::uint32_t>(std::max(available.y, 1.0F))),
        };
    }

    struct SceneOverlayLabel {
        std::string_view key;
        std::string_view fallback;
    };

    struct SceneOverlayStripResult {
        bool hovered{};
        bool changed{};
    };

    [[nodiscard]] bool* sceneOverlayFlagForId(asharia::editor::EditorViewportOverlayFlags& flags,
                                              std::string_view overlayId) {
        if (overlayId == "scene.grid") {
            return &flags.gridVisible;
        }
        if (overlayId == "scene.transform-gizmo") {
            return &flags.gizmoVisible;
        }
        if (overlayId == "scene.selection-outline") {
            return &flags.selectionOutlineVisible;
        }
        return nullptr;
    }

    [[nodiscard]] SceneOverlayLabel sceneOverlayLabelForId(std::string_view overlayId) {
        if (overlayId == "scene.grid") {
            return SceneOverlayLabel{.key = "scene.overlay.grid", .fallback = "Grid"};
        }
        if (overlayId == "scene.transform-gizmo") {
            return SceneOverlayLabel{.key = "scene.overlay.gizmo", .fallback = "Gizmo"};
        }
        if (overlayId == "scene.selection-outline") {
            return SceneOverlayLabel{.key = "scene.overlay.selection", .fallback = "Select"};
        }
        return SceneOverlayLabel{.key = {}, .fallback = overlayId};
    }

    [[nodiscard]] bool
    drawSceneOverlayToggle(const asharia::editor::EditorI18n& i18n,
                           const asharia::editor::EditorToolViewportOverlayContribution& overlay,
                           bool& value) {
        const SceneOverlayLabel text = sceneOverlayLabelForId(overlay.overlayId);
        const std::string label = i18n.label(asharia::editor::EditorI18nLabelDesc{
            .key = text.key,
            .stableId = overlay.overlayId,
            .fallback = text.fallback,
        });

        if (value) {
            const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  asharia::editor::toImGuiEncodedSrgbVec4(theme.accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  asharia::editor::toImGuiEncodedSrgbVec4(theme.accentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  asharia::editor::toImGuiEncodedSrgbVec4(theme.accentActive));
        }

        const bool pressed = ImGui::Button(label.c_str());
        if (value) {
            ImGui::PopStyleColor(3);
        }
        if (pressed) {
            value = !value;
        }
        return pressed;
    }

    [[nodiscard]] SceneOverlayStripResult
    drawSceneOverlayStrip(const asharia::editor::EditorFrameContext& context,
                          std::string_view viewportId, ImVec2 viewportMin, ImVec2 viewportMax,
                          asharia::editor::EditorViewportOverlayFlags& flags) {
        constexpr float kOverlayPadding = 8.0F;
        const ImVec2 stripPadding{6.0F, 4.0F};
        const ImVec2 buttonPadding{6.0F, 2.0F};
        const ImGuiStyle& style = ImGui::GetStyle();
        float controlsWidth = 0.0F;
        int controlCount = 0;
        context.tools.visitViewportOverlays(
            viewportId, [&](const asharia::editor::EditorToolDesc& tool,
                            const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                if (sceneOverlayFlagForId(flags, overlay.overlayId) == nullptr) {
                    return;
                }
                const SceneOverlayLabel text = sceneOverlayLabelForId(overlay.overlayId);
                const std::string label = context.i18n.label(asharia::editor::EditorI18nLabelDesc{
                    .key = text.key,
                    .stableId = overlay.overlayId,
                    .fallback = text.fallback,
                });
                controlsWidth += ImGui::CalcTextSize(label.c_str()).x + (buttonPadding.x * 2.0F);
                ++controlCount;
            });

        if (viewportMax.x <= viewportMin.x + (kOverlayPadding * 2.0F) ||
            viewportMax.y <= viewportMin.y + (kOverlayPadding * 2.0F) || controlCount == 0) {
            return {};
        }
        controlsWidth += style.ItemSpacing.x * static_cast<float>(std::max(0, controlCount - 1));

        const ImVec2 stripPos{viewportMin.x + kOverlayPadding, viewportMin.y + kOverlayPadding};
        const ImVec2 stripSize{controlsWidth + (stripPadding.x * 2.0F),
                               ImGui::GetTextLineHeight() + (buttonPadding.y * 2.0F) +
                                   (stripPadding.y * 2.0F)};
        const ImVec2 stripMax{stripPos.x + stripSize.x, stripPos.y + stripSize.y};
        const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(stripPos, stripMax,
                                asharia::editor::toImGuiEncodedSrgbU32(theme.floatingBackground),
                                4.0F);
        drawList->AddRect(stripPos, stripMax,
                          asharia::editor::toImGuiEncodedSrgbU32(theme.borderStrong), 4.0F);

        SceneOverlayStripResult result;
        const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2{stripPos.x + stripPadding.x, stripPos.y + stripPadding.y});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, buttonPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0F, 0.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0F);
        ImGui::BeginGroup();
        bool drewToggle = false;
        context.tools.visitViewportOverlays(
            viewportId, [&](const asharia::editor::EditorToolDesc& tool,
                            const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                bool* flag = sceneOverlayFlagForId(flags, overlay.overlayId);
                if (flag == nullptr) {
                    return;
                }
                if (drewToggle) {
                    ImGui::SameLine();
                }
                result.changed =
                    drawSceneOverlayToggle(context.i18n, overlay, *flag) || result.changed;
                drewToggle = true;
            });
        ImGui::EndGroup();
        ImGui::PopStyleVar(3);
        result.hovered = ImGui::IsMouseHoveringRect(stripPos, stripMax, false);
        ImGui::SetCursorScreenPos(restoreCursor);
        return result;
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& SceneViewPanel::desc() const {
        return desc_;
    }

    void SceneViewPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const ImGuiCond sceneWindowCond =
            context.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        if (const ImGuiViewport* mainViewport = ImGui::GetMainViewport(); mainViewport != nullptr) {
            ImGui::SetNextWindowPos(
                ImVec2{mainViewport->WorkPos.x + 8.0F, mainViewport->WorkPos.y + 8.0F},
                sceneWindowCond);
        }
        ImGui::SetNextWindowSize(
            ImVec2{std::max(320.0F, static_cast<float>(context.swapchainExtent.width) * 0.60F),
                   std::max(240.0F, static_cast<float>(context.swapchainExtent.height) * 0.62F)},
            sceneWindowCond);
    }

    void SceneViewPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        viewportSize.y =
            std::max(1.0F, viewportSize.y - (ImGui::GetTextLineHeightWithSpacing() * 3.0F));
        const EditorExtent2D viewportExtent = viewportExtentFromAvailableSize(viewportSize);
        std::uint64_t viewportFrameIndex{};
        if (const auto completed =
                context.viewportHost.acquireViewportTextureForDraw(desc_.id.value);
            completed && hasEditorViewportTexture(completed->texture)) {
            viewportFrameIndex = completed->texture.frameIndex;
            ImGui::Image(static_cast<ImTextureID>(completed->texture.textureId),
                         ImVec2{static_cast<float>(viewportExtent.width),
                                static_cast<float>(viewportExtent.height)});
        } else {
            ImGui::Dummy(ImVec2{static_cast<float>(viewportExtent.width),
                                static_cast<float>(viewportExtent.height)});
        }
        const ImVec2 viewportMin = ImGui::GetItemRectMin();
        const ImVec2 viewportMax = ImGui::GetItemRectMax();
        const bool viewportHovered = ImGui::IsItemHovered();
        const SceneOverlayStripResult overlayStrip =
            drawSceneOverlayStrip(context, desc_.id.value, viewportMin, viewportMax, overlayFlags_);
        context.inputRouter.reportSceneView(EditorSceneViewInputState{
            .hovered = viewportHovered && !overlayStrip.hovered,
            .focused = state.focused,
        });

        EditorViewportRefreshRequest refresh{
            .policy = EditorViewportRefreshPolicy::OnDemand,
        };
        if (overlayStrip.changed) {
            addEditorViewportRepaintReason(refresh.repaintReasons,
                                           EditorViewportRepaintReason::OverlayFlagsChanged);
        }
        context.viewportHost.requestViewport(EditorViewportRequest{
            .panelId = desc_.id,
            .kind = EditorViewportKind::Scene,
            .extent = viewportExtent,
            .overlayFlags = overlayFlags_,
            .refresh = refresh,
        });

        const EditorI18n& i18n = context.i18n;
        const std::string swapchainText = std::string{i18n.text("scene.swapchain")} + ": " +
                                          std::to_string(context.swapchainExtent.width) + "x" +
                                          std::to_string(context.swapchainExtent.height);
        const std::string viewportText = std::string{i18n.text("scene.viewport")} + ": " +
                                         std::to_string(viewportExtent.width) + "x" +
                                         std::to_string(viewportExtent.height);
        const std::string frameText = std::string{i18n.text("scene.viewportFrame")} + ": " +
                                      std::to_string(viewportFrameIndex);
        ImGui::TextUnformatted(swapchainText.c_str());
        ImGui::TextUnformatted(viewportText.c_str());
        ImGui::TextUnformatted(frameText.c_str());
    }

} // namespace asharia::editor

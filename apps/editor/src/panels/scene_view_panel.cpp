#include "panels/scene_view_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_viewport.hpp"

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

    struct SceneViewPanelContext {
        const asharia::editor::EditorFrameUiContext* ui{};
        const asharia::editor::EditorToolRegistry* tools{};
        asharia::editor::EditorInputRouter* inputRouter{};
        asharia::editor::EditorViewportPanelHost* viewportHost{};
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
    drawSceneOverlayStrip(const asharia::editor::EditorFrameUiContext& uiContext,
                          const asharia::editor::EditorToolRegistry& tools,
                          std::string_view viewportId, ImVec2 viewportMin, ImVec2 viewportMax,
                          asharia::editor::EditorViewportOverlayFlags& flags) {
        constexpr float kOverlayPadding = 8.0F;
        const ImVec2 stripPadding{6.0F, 4.0F};
        const ImVec2 buttonPadding{6.0F, 2.0F};
        const ImGuiStyle& style = ImGui::GetStyle();
        float controlsWidth = 0.0F;
        int controlCount = 0;
        tools.visitViewportOverlays(
            viewportId, [&](const asharia::editor::EditorToolDesc& tool,
                            const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                if (sceneOverlayFlagForId(flags, overlay.overlayId) == nullptr) {
                    return;
                }
                const SceneOverlayLabel text = sceneOverlayLabelForId(overlay.overlayId);
                const std::string label = uiContext.i18n.label(asharia::editor::EditorI18nLabelDesc{
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
        tools.visitViewportOverlays(
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
                    drawSceneOverlayToggle(uiContext.i18n, overlay, *flag) || result.changed;
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

    void SceneViewPanel::prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const ImGuiCond sceneWindowCond =
            context.ui.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        if (const ImGuiViewport* mainViewport = ImGui::GetMainViewport(); mainViewport != nullptr) {
            ImGui::SetNextWindowPos(
                ImVec2{mainViewport->WorkPos.x + 8.0F, mainViewport->WorkPos.y + 8.0F},
                sceneWindowCond);
        }
        ImGui::SetNextWindowSize(
            ImVec2{std::max(320.0F, static_cast<float>(context.ui.swapchainExtent.width) * 0.60F),
                   std::max(240.0F, static_cast<float>(context.ui.swapchainExtent.height) * 0.62F)},
            sceneWindowCond);
    }

    void SceneViewPanel::updateCameraForViewportExtent(EditorExtent2D viewportExtent) {
        if (!cameraInitialized_) {
            camera_ = defaultEditorSceneViewCamera(viewportExtent);
            cameraExtent_ = viewportExtent;
            cameraInitialized_ = true;
            return;
        }
        if (cameraExtent_.width == viewportExtent.width &&
            cameraExtent_.height == viewportExtent.height) {
            return;
        }
        camera_ = editorViewportCameraForExtent(camera_, viewportExtent);
        cameraExtent_ = viewportExtent;
    }

    bool SceneViewPanel::handleCameraNavigation(EditorExtent2D viewportExtent) {
        const bool viewportHovered = ImGui::IsItemHovered();
        const ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        const ImVec2 panDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        const float scrollDelta = ImGui::GetIO().MouseWheel;

        if (!viewportHovered) {
            return false;
        }

        bool cameraChanged = false;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) &&
            (std::fabs(mouseDelta.x) > 0.0F || std::fabs(mouseDelta.y) > 0.0F)) {
            constexpr float kOrbitSpeed = 0.005F;
            orbitEditorViewportCamera(camera_, mouseDelta.x * kOrbitSpeed,
                                      mouseDelta.y * kOrbitSpeed);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            cameraChanged = true;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) &&
            (std::fabs(panDelta.x) > 0.0F || std::fabs(panDelta.y) > 0.0F)) {
            panEditorViewportCamera(camera_, panDelta.x, panDelta.y, viewportExtent);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            cameraChanged = true;
        }
        if (std::fabs(scrollDelta) > 0.0F) {
            dollyEditorViewportCamera(camera_, scrollDelta);
            cameraChanged = true;
        }
        if (cameraChanged) {
            camera_ = editorViewportCameraForExtent(camera_, viewportExtent);
        }
        return cameraChanged;
    }

    void SceneViewPanel::drawSceneViewPanel(EditorSceneViewPanelDrawContext& context,
                                            EditorPanelState& state) {
        SceneViewPanelContext panelContext{
            .ui = &context.ui,
            .tools = &context.tools,
            .inputRouter = &context.inputRouter,
            .viewportHost = &context.viewportHost,
        };

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        viewportSize.y =
            std::max(1.0F, viewportSize.y - (ImGui::GetTextLineHeightWithSpacing() * 3.0F));
        const EditorExtent2D viewportExtent = viewportExtentFromAvailableSize(viewportSize);
        updateCameraForViewportExtent(viewportExtent);
        std::uint64_t viewportFrameIndex{};
        if (const auto completed = panelContext.viewportHost->acquireViewportTextureForDraw(
                desc_.id.value, EditorViewportKind::Scene);
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
        const bool cameraChanged = handleCameraNavigation(viewportExtent);
        const SceneOverlayStripResult overlayStrip =
            drawSceneOverlayStrip(*panelContext.ui, *panelContext.tools, desc_.id.value,
                                  viewportMin, viewportMax, overlayFlags_);
        panelContext.inputRouter->reportSceneView(EditorSceneViewInputState{
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
        if (cameraChanged) {
            addEditorViewportRepaintReason(refresh.repaintReasons,
                                           EditorViewportRepaintReason::CameraInputChanged);
        }
        panelContext.viewportHost->requestViewport(EditorViewportRequest{
            .panelId = desc_.id,
            .kind = EditorViewportKind::Scene,
            .extent = viewportExtent,
            .camera = camera_,
            .overlayFlags = overlayFlags_,
            .refresh = refresh,
        });

        const EditorI18n& i18n = panelContext.ui->i18n;
        const std::string swapchainText = std::string{i18n.text("scene.swapchain")} + ": " +
                                          std::to_string(panelContext.ui->swapchainExtent.width) +
                                          "x" +
                                          std::to_string(panelContext.ui->swapchainExtent.height);
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

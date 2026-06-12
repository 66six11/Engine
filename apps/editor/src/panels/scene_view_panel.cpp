#include "panels/scene_view_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_asset_icon.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_settings.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_overlay_provider.hpp"

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
        const asharia::editor::EditorSettings* settings{};
        const asharia::editor::EditorFrameToolContext* tools{};
        asharia::editor::EditorInputRouter* inputRouter{};
        asharia::editor::EditorViewportPanelHost* viewportHost{};
    };

    struct SceneViewportStatsOverlayText {
        std::string_view swapchain;
        std::string_view viewport;
        std::string_view frame;
    };

    void drawSceneViewportStatsOverlay(SceneViewportStatsOverlayText text, ImVec2 viewportMin,
                                       ImVec2 viewportMax) {
        const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
        const std::string first{text.swapchain};
        const std::string second{text.viewport};
        const std::string third{text.frame};
        const float width =
            std::max({ImGui::CalcTextSize(first.c_str()).x, ImGui::CalcTextSize(second.c_str()).x,
                      ImGui::CalcTextSize(third.c_str()).x}) +
            14.0F;
        const float lineHeight = ImGui::GetTextLineHeight();
        const float height = (lineHeight * 3.0F) + 10.0F;
        const ImVec2 min{std::max(viewportMin.x + 8.0F, viewportMax.x - width - 8.0F),
                         std::max(viewportMin.y + 8.0F, viewportMax.y - height - 8.0F)};
        const ImVec2 max{min.x + width, min.y + height};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            min, max,
            ImGui::GetColorU32(asharia::editor::toImGuiEncodedSrgbVec4(asharia::editor::ColorSrgba8{
                .r = theme.inputBackground.r,
                .g = theme.inputBackground.g,
                .b = theme.inputBackground.b,
                .a = 205,
            })),
            2.0F);
        drawList->AddRect(min, max, asharia::editor::toImGuiEncodedSrgbU32(theme.border), 2.0F);
        const ImU32 textColor = asharia::editor::toImGuiEncodedSrgbU32(theme.textMuted);
        drawList->AddText(ImVec2{min.x + 7.0F, min.y + 5.0F}, textColor, first.c_str());
        drawList->AddText(ImVec2{min.x + 7.0F, min.y + 5.0F + lineHeight}, textColor,
                          second.c_str());
        drawList->AddText(ImVec2{min.x + 7.0F, min.y + 5.0F + (lineHeight * 2.0F)}, textColor,
                          third.c_str());
    }

    [[nodiscard]] bool* sceneOverlayFlagForId(asharia::editor::EditorViewportOverlayFlags& flags,
                                              std::string_view overlayId) {
        if (overlayId == asharia::editor::kEditorSceneGridOverlayId) {
            return &flags.gridVisible;
        }
        if (overlayId == asharia::editor::kEditorSceneTransformGizmoOverlayId) {
            return &flags.gizmoVisible;
        }
        if (overlayId == asharia::editor::kEditorSceneSelectionOutlineOverlayId) {
            return &flags.selectionOutlineVisible;
        }
        return nullptr;
    }

    [[nodiscard]] SceneOverlayLabel sceneOverlayLabelForId(std::string_view overlayId) {
        if (overlayId == asharia::editor::kEditorSceneGridOverlayId) {
            return SceneOverlayLabel{.key = "scene.overlay.grid", .fallback = "Grid"};
        }
        if (overlayId == asharia::editor::kEditorSceneTransformGizmoOverlayId) {
            return SceneOverlayLabel{.key = "scene.overlay.gizmo", .fallback = "Gizmo"};
        }
        if (overlayId == asharia::editor::kEditorSceneSelectionOutlineOverlayId) {
            return SceneOverlayLabel{.key = "scene.overlay.selection", .fallback = "Select"};
        }
        return SceneOverlayLabel{.key = {}, .fallback = overlayId};
    }

    [[nodiscard]] std::string_view sceneOverlayIconForId(std::string_view overlayId) {
        if (overlayId == asharia::editor::kEditorSceneGridOverlayId) {
            return "grid-3x3";
        }
        if (overlayId == asharia::editor::kEditorSceneTransformGizmoOverlayId) {
            return "move-3d";
        }
        if (overlayId == asharia::editor::kEditorSceneSelectionOutlineOverlayId) {
            return "eye";
        }
        return "circle-help";
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
        const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
        const auto tint =
            asharia::editor::editorIconTint(static_cast<float>(theme.textSecondary.r) / 255.0F,
                                            static_cast<float>(theme.textSecondary.g) / 255.0F,
                                            static_cast<float>(theme.textSecondary.b) / 255.0F);
        std::string tooltip = label;
        if (!overlay.enabled &&
            (!overlay.disabledReasonKey.empty() || !overlay.disabledReasonFallback.empty())) {
            tooltip += "\nDisabled: ";
            tooltip += std::string{i18n.text(asharia::editor::EditorI18nTextQuery{
                .key = overlay.disabledReasonKey,
                .fallback = overlay.disabledReasonFallback,
            })};
        }
        const asharia::editor::EditorIconDescriptor icon =
            asharia::editor::makeLucideEditorIconDescriptor(
                sceneOverlayIconForId(overlay.overlayId), tint, {}, tooltip);

        if (!overlay.enabled) {
            const bool changed = value;
            value = false;

            static_cast<void>(asharia::editor::drawEditorUiIconButton(icon, overlay.overlayId,
                                                                      false, false, tooltip));
            return changed;
        }

        const bool pressed =
            asharia::editor::drawEditorUiIconButton(icon, overlay.overlayId, value);
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
        const ImGuiStyle& style = ImGui::GetStyle();
        const asharia::editor::EditorUiMetrics& metrics = asharia::editor::editorUiMetrics();
        float controlsWidth = 0.0F;
        int controlCount = 0;
        tools.visitViewportOverlays(
            viewportId, [&](const asharia::editor::EditorToolDesc& tool,
                            const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                if (sceneOverlayFlagForId(flags, overlay.overlayId) == nullptr) {
                    return;
                }
                controlsWidth += metrics.toolbarButtonSize;
                ++controlCount;
            });

        if (viewportMax.x <= viewportMin.x + (kOverlayPadding * 2.0F) ||
            viewportMax.y <= viewportMin.y + (kOverlayPadding * 2.0F) || controlCount == 0) {
            return {};
        }
        controlsWidth += style.ItemSpacing.x * static_cast<float>(std::max(0, controlCount - 1));

        const ImVec2 stripPos{viewportMin.x + kOverlayPadding, viewportMin.y + kOverlayPadding};
        const ImVec2 stripSize{controlsWidth + (stripPadding.x * 2.0F),
                               metrics.toolbarButtonSize + (stripPadding.y * 2.0F)};
        const ImVec2 stripMax{stripPos.x + stripSize.x, stripPos.y + stripSize.y};
        const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(stripPos, stripMax,
                                asharia::editor::toImGuiEncodedSrgbU32(theme.floatingBackground),
                                4.0F);
        drawList->AddRect(stripPos, stripMax,
                          asharia::editor::toImGuiEncodedSrgbU32(theme.borderStrong), 4.0F);

        SceneOverlayStripResult result;
        ImGui::SetCursorScreenPos(ImVec2{stripPos.x + stripPadding.x, stripPos.y + stripPadding.y});
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
        ImGui::PopStyleVar(2);
        result.hovered = ImGui::IsMouseHoveringRect(stripPos, stripMax, false);
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

    bool SceneViewPanel::handleCameraNavigation(EditorExtent2D viewportExtent,
                                                const EditorInputSnapshot& input) {
        if (!input.sceneViewCanReceiveMouse) {
            return false;
        }

        bool cameraChanged = false;
        if (input.sceneViewCameraOrbitActive) {
            constexpr float kOrbitSpeed = 0.005F;
            orbitEditorViewportCamera(camera_, input.sceneViewCameraOrbitDeltaX * kOrbitSpeed,
                                      input.sceneViewCameraOrbitDeltaY * kOrbitSpeed);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            cameraChanged = true;
        }
        if (input.sceneViewCameraPanActive) {
            panEditorViewportCamera(camera_, input.sceneViewCameraPanDeltaX,
                                    input.sceneViewCameraPanDeltaY, viewportExtent);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            cameraChanged = true;
        }
        if (input.sceneViewCameraDollyActive) {
            dollyEditorViewportCamera(camera_, input.sceneViewCameraDollyDelta);
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
            .settings = &context.settings,
            .tools = &context.tools,
            .inputRouter = &context.inputRouter,
            .viewportHost = &context.viewportHost,
        };

        const EditorI18n& i18n = panelContext.ui->i18n;
        const std::string sceneHeaderStatus = std::string{i18n.text("scene.viewMode.shaded")} +
                                              " | " + std::string{i18n.text("scene.pivot")} +
                                              " | " + std::string{i18n.text("scene.space.local")};
        asharia::editor::drawEditorUiPanelHeader(i18n.text("panel.sceneView"), sceneHeaderStatus);
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        viewportSize.y = std::max(1.0F, viewportSize.y);
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
        const std::string swapchainText = std::string{i18n.text("scene.swapchain")} + " " +
                                          std::to_string(panelContext.ui->swapchainExtent.width) +
                                          "x" +
                                          std::to_string(panelContext.ui->swapchainExtent.height);
        const std::string viewportText = std::string{i18n.text("scene.viewport")} + " " +
                                         std::to_string(viewportExtent.width) + "x" +
                                         std::to_string(viewportExtent.height);
        const std::string frameText = std::string{i18n.text("scene.viewportFrame")} + " " +
                                      std::to_string(viewportFrameIndex);
        drawSceneViewportStatsOverlay(
            SceneViewportStatsOverlayText{
                .swapchain = swapchainText,
                .viewport = viewportText,
                .frame = frameText,
            },
            viewportMin, viewportMax);
        const SceneOverlayStripResult overlayStrip =
            drawSceneOverlayStrip(*panelContext.ui, panelContext.tools->registry, desc_.id.value,
                                  viewportMin, viewportMax, overlayFlags_);
        panelContext.inputRouter->reportSceneView(EditorSceneViewInputState{
            .hovered = viewportHovered && !overlayStrip.hovered,
            .focused = state.focused,
        });
        const bool cameraChanged =
            handleCameraNavigation(viewportExtent, panelContext.inputRouter->snapshot());

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
            .worldGrid = panelContext.settings->sceneGrid,
            .refresh = refresh,
        });
    }

} // namespace asharia::editor

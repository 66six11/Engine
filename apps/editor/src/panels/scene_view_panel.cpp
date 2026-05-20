#include "panels/scene_view_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <imgui.h>
#include <string>

#include "editor_input_router.hpp"

namespace {

    asharia::editor::EditorExtent2D viewportExtentFromAvailableSize(ImVec2 available) {
        return asharia::editor::EditorExtent2D{
            .width = std::max(1U, static_cast<std::uint32_t>(std::max(available.x, 1.0F))),
            .height = std::max(1U, static_cast<std::uint32_t>(std::max(available.y, 1.0F))),
        };
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
        context.viewportHost.requestViewport(EditorViewportRequest{
            .panelId = desc_.id,
            .kind = EditorViewportKind::Scene,
            .extent = viewportExtent,
        });
        if (const auto completed =
                context.viewportHost.acquireViewportTextureForDraw(desc_.id.value);
            completed && hasEditorViewportTexture(completed->texture)) {
            ImGui::Image(static_cast<ImTextureID>(completed->texture.textureId),
                         ImVec2{static_cast<float>(viewportExtent.width),
                                static_cast<float>(viewportExtent.height)});
        } else {
            ImGui::Dummy(ImVec2{static_cast<float>(viewportExtent.width),
                                static_cast<float>(viewportExtent.height)});
        }
        context.inputRouter.reportSceneView(EditorSceneViewInputState{
            .hovered = ImGui::IsItemHovered(),
            .focused = state.focused,
        });

        const std::string swapchainText =
            "Swapchain: " + std::to_string(context.swapchainExtent.width) + "x" +
            std::to_string(context.swapchainExtent.height);
        const std::string viewportText = "Viewport: " + std::to_string(viewportExtent.width) + "x" +
                                         std::to_string(viewportExtent.height);
        const std::string frameText = "Frame: " + std::to_string(context.frameIndex);
        ImGui::TextUnformatted(swapchainText.c_str());
        ImGui::TextUnformatted(viewportText.c_str());
        ImGui::TextUnformatted(frameText.c_str());
    }

} // namespace asharia::editor

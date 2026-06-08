#include "editor_dock_layout.hpp"

#include <imgui_internal.h>
#include <string>

#include "editor_i18n.hpp"

namespace asharia::editor {

    namespace {

        struct EditorDockLayoutNodes {
            ImGuiID left{};
            ImGuiID center{};
            ImGuiID rightTop{};
            ImGuiID rightBottom{};
            ImGuiID bottom{};
        };

        [[nodiscard]] ImGuiID dockNodeForSlot(const EditorDockLayoutNodes& nodes,
                                              EditorDockSlot slot) {
            switch (slot) {
            case EditorDockSlot::Left:
                return nodes.left;
            case EditorDockSlot::Center:
                return nodes.center;
            case EditorDockSlot::RightTop:
                return nodes.rightTop;
            case EditorDockSlot::RightBottom:
                return nodes.rightBottom;
            case EditorDockSlot::Bottom:
                return nodes.bottom;
            }
            return nodes.center;
        }

        void dockPanelWindow(const EditorPanelRegistry& panelRegistry, const EditorI18n& i18n,
                             std::string_view panelId, ImGuiID dockId) {
            const std::string windowTitle = panelRegistry.panelWindowTitle(panelId, i18n);
            ImGui::DockBuilderDockWindow(windowTitle.c_str(), dockId);
        }

    } // namespace

    bool editorDockLayoutExists(ImGuiID dockspaceId) {
        return ImGui::DockBuilderGetNode(dockspaceId) != nullptr;
    }

    void buildEditorDockLayout(const EditorDockLayoutBuildDesc& desc) {
        ImGui::DockBuilderRemoveNode(desc.dockspaceId);
        ImGui::DockBuilderAddNode(desc.dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(desc.dockspaceId, desc.viewport.WorkPos);
        ImGui::DockBuilderSetNodeSize(desc.dockspaceId, desc.viewport.WorkSize);

        ImGuiID mainDockId = desc.dockspaceId;
        ImGuiID bottomDockId = 0;
        ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Down, desc.preset.bottomRatio,
                                    &bottomDockId, &mainDockId);

        ImGuiID leftDockId = 0;
        ImGuiID centerDockId = mainDockId;
        ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Left, desc.preset.leftRatio,
                                    &leftDockId, &centerDockId);

        ImGuiID rightDockId = 0;
        ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Right, desc.preset.rightRatio,
                                    &rightDockId, &centerDockId);

        ImGuiID rightBottomDockId = 0;
        ImGuiID rightTopDockId = rightDockId;
        ImGui::DockBuilderSplitNode(rightTopDockId, ImGuiDir_Down,
                                    desc.preset.rightBottomRatio, &rightBottomDockId,
                                    &rightTopDockId);

        const EditorDockLayoutNodes nodes{
            .left = leftDockId,
            .center = centerDockId,
            .rightTop = rightTopDockId,
            .rightBottom = rightBottomDockId,
            .bottom = bottomDockId,
        };
        for (const EditorWorkspaceDockPanel& panel : desc.preset.panels) {
            dockPanelWindow(desc.panelRegistry, desc.i18n, panel.panelId,
                            dockNodeForSlot(nodes, panel.dockSlot));
        }

        ImGui::DockBuilderFinish(desc.dockspaceId);
    }

} // namespace asharia::editor

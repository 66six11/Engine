#include "panels/scene_tree_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace {

    [[nodiscard]] std::string textValue(const asharia::editor::EditorI18n& i18n,
                                        std::string_view key, std::string_view fallback) {
        return std::string{i18n.text(asharia::editor::EditorI18nTextQuery{
            .key = key,
            .fallback = fallback,
        })};
    }

    [[nodiscard]] std::string label(const asharia::editor::EditorI18n& i18n, std::string_view key,
                                    std::string_view stableId, std::string_view fallback) {
        return i18n.label(asharia::editor::EditorI18nLabelDesc{
            .key = key,
            .stableId = stableId,
            .fallback = fallback,
        });
    }

    void mutedText(std::string_view value) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
        ImGui::PopStyleColor();
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& SceneTreePanel::desc() const {
        return desc_;
    }

    void SceneTreePanel::drawSceneTreePanel(EditorSceneTreePanelDrawContext& context,
                                            EditorPanelState& state) {
        static_cast<void>(state);

        const EditorI18n& i18n = context.ui.i18n;
        const std::string filterLabel =
            label(i18n, "sceneTree.filter", "scene-tree-filter", "Filter");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::BeginDisabled();
        ImGui::InputText(filterLabel.c_str(), filter_.data(), filter_.size());
        ImGui::EndDisabled();

        if (beginEditorUiPropertyTable("scene-tree-shell-state", 88.0F)) {
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "sceneTree.selection", "Selection"),
                .value = textValue(i18n, "sceneTree.selection.none", "None"),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "sceneTree.source", "Source"),
                .value = textValue(i18n, "sceneTree.source.pending", "Scene model pending"),
            });
            endEditorUiPropertyTable();
        }

        ImGui::Separator();
        drawEditorUiStatusPill(textValue(i18n, "sceneTree.state.shell", "Shell"),
                               EditorUiTone::Muted);
        ImGui::SameLine();
        drawEditorUiStatusPill(textValue(i18n, "sceneTree.state.readOnly", "Read-only"),
                               EditorUiTone::Info);
        ImGui::Spacing();

        const ImGuiTreeNodeFlags rootFlags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        const std::string rootLabel =
            textValue(i18n, "sceneTree.root", "Inspected World") + "###scene-tree-root";
        if (ImGui::TreeNodeEx(rootLabel.c_str(), rootFlags)) {
            ImGui::BeginDisabled();
            const std::string pendingLabel =
                textValue(i18n, "sceneTree.pendingIdentity", "Scene object identity bridge");
            ImGui::TreeNodeEx(pendingLabel.c_str(),
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth);
            const std::string selectionLabel =
                textValue(i18n, "sceneTree.pendingSelection", "SelectionSet");
            ImGui::TreeNodeEx(selectionLabel.c_str(),
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        ImGui::Spacing();
        mutedText(textValue(i18n, "sceneTree.empty", "No scene hierarchy loaded."));
    }

} // namespace asharia::editor

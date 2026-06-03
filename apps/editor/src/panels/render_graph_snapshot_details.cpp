#include "panels/render_graph_snapshot_details.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "panels/render_graph_snapshot_format.hpp"

namespace {

    using namespace asharia::editor::render_graph_snapshot_format;

    constexpr float kDetailMinHeight = 120.0F;
    constexpr float kDetailMaxHeight = 180.0F;

    constexpr ImGuiTableFlags kDetailTableFlags =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    void tableText(std::string_view text) {
        const std::string copy{text};
        ImGui::TextUnformatted(copy.c_str());
    }

    void disabledText(std::string_view text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        tableText(text);
        ImGui::PopStyleColor();
    }

    [[nodiscard]] float detailHeight() {
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        if (availableHeight <= 0.0F) {
            return 150.0F;
        }
        return std::clamp(availableHeight * 0.48F, kDetailMinHeight, kDetailMaxHeight);
    }

    void drawAccessEventsList(const asharia::editor::EditorI18n& i18n,
                              const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                              float height) {
        tableText(i18n.text("renderGraph.accessEvents"));
        if (snapshot.accessEdges.empty()) {
            disabledText(i18n.text("renderGraph.noAccessEvents"));
            return;
        }

        if (!ImGui::BeginTable("rg-access-events", 5, kDetailTableFlags, ImVec2{0.0F, height})) {
            return;
        }
        const std::string passColumn{i18n.text("renderGraph.pass")};
        const std::string resourceColumn{i18n.text("renderGraph.resource")};
        const std::string slotColumn{i18n.text("renderGraph.slot")};
        const std::string useColumn{i18n.text("renderGraph.use")};
        const std::string directionColumn{i18n.text("renderGraph.direction")};
        ImGui::TableSetupColumn(passColumn.c_str());
        ImGui::TableSetupColumn(resourceColumn.c_str());
        ImGui::TableSetupColumn(slotColumn.c_str());
        ImGui::TableSetupColumn(useColumn.c_str());
        ImGui::TableSetupColumn(directionColumn.c_str());
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText("#" + std::to_string(edge.passIndex) + " " + fallbackText(edge.passName));
            ImGui::TableNextColumn();
            tableText(resourceLabel(edge.resourceKind, edge.resourceIndex, edge.resourceName));
            ImGui::TableNextColumn();
            tableText(edge.slotName);
            ImGui::TableNextColumn();
            tableText(accessRoleName(edge.access));
            ImGui::TableNextColumn();
            tableText(accessDirectionName(edge.access));
        }
        ImGui::EndTable();
    }

    void drawResourceList(const asharia::editor::EditorI18n& i18n,
                          const asharia::RenderGraphDiagnosticsSnapshot& snapshot, float height) {
        tableText(i18n.text("renderGraph.resourceList"));
        if (!ImGui::BeginTable("rg-resource-list", 5, kDetailTableFlags, ImVec2{0.0F, height})) {
            return;
        }
        ImGui::TableSetupColumn("#");
        const std::string nameColumn{i18n.text("renderGraph.name")};
        const std::string typeColumn{i18n.text("renderGraph.type")};
        const std::string shapeColumn{i18n.text("renderGraph.shape")};
        const std::string lifetimeStateColumn{i18n.text("renderGraph.lifetimeState")};
        ImGui::TableSetupColumn(nameColumn.c_str());
        ImGui::TableSetupColumn(typeColumn.c_str());
        ImGui::TableSetupColumn(shapeColumn.c_str());
        ImGui::TableSetupColumn(lifetimeStateColumn.c_str());
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsResourceNode& resource : snapshot.resources) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText(std::to_string(resource.resourceIndex));
            ImGui::TableNextColumn();
            tableText(resource.name);
            ImGui::TableNextColumn();
            tableText(resourceKindName(resource.kind));
            ImGui::TableNextColumn();
            tableText(resourceShape(resource));
            ImGui::TableNextColumn();
            tableText(resourceLifetime(resource) + " / " + resourceAccessRange(resource));
        }
        ImGui::EndTable();
    }

    void drawPassList(const asharia::editor::EditorI18n& i18n,
                      const asharia::RenderGraphDiagnosticsSnapshot& snapshot, float height) {
        tableText(i18n.text("renderGraph.passList"));
        if (!ImGui::BeginTable("rg-pass-list", 6, kDetailTableFlags, ImVec2{0.0F, height})) {
            return;
        }
        ImGui::TableSetupColumn("#");
        const std::string nameColumn{i18n.text("renderGraph.name")};
        const std::string typeColumn{i18n.text("renderGraph.type")};
        const std::string commandsColumn{i18n.text("renderGraph.commands")};
        const std::string transitionsColumn{i18n.text("renderGraph.transitions")};
        const std::string cullableColumn{i18n.text("renderGraph.cullable")};
        ImGui::TableSetupColumn(nameColumn.c_str());
        ImGui::TableSetupColumn(typeColumn.c_str());
        ImGui::TableSetupColumn(commandsColumn.c_str());
        ImGui::TableSetupColumn(transitionsColumn.c_str());
        ImGui::TableSetupColumn(cullableColumn.c_str());
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.passIndex));
            ImGui::TableNextColumn();
            tableText(pass.name);
            ImGui::TableNextColumn();
            tableText(fallbackText(pass.type));
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.commandCount));
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.imageTransitionCount + pass.bufferTransitionCount));
            ImGui::TableNextColumn();
            tableText(boolName(pass.allowCulling));
        }
        ImGui::EndTable();
    }

    void drawDependencyList(const asharia::editor::EditorI18n& i18n,
                            const asharia::RenderGraphDiagnosticsSnapshot& snapshot, float height) {
        tableText(i18n.text("renderGraph.dependencies"));
        if (snapshot.dependencyEdges.empty()) {
            disabledText(i18n.text("renderGraph.noDependencies"));
            return;
        }

        if (!ImGui::BeginTable("rg-dependency-list", 4, kDetailTableFlags, ImVec2{0.0F, height})) {
            return;
        }
        const std::string fromColumn{i18n.text("renderGraph.from")};
        const std::string toColumn{i18n.text("renderGraph.to")};
        const std::string resourceColumn{i18n.text("renderGraph.resource")};
        const std::string reasonColumn{i18n.text("renderGraph.reason")};
        ImGui::TableSetupColumn(fromColumn.c_str());
        ImGui::TableSetupColumn(toColumn.c_str());
        ImGui::TableSetupColumn(resourceColumn.c_str());
        ImGui::TableSetupColumn(reasonColumn.c_str());
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsDependencyEdge& dependency :
             snapshot.dependencyEdges) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText("#" + std::to_string(dependency.fromPassIndex));
            ImGui::TableNextColumn();
            tableText("#" + std::to_string(dependency.toPassIndex));
            ImGui::TableNextColumn();
            tableText(resourceLabel(dependency.resourceKind, dependency.resourceIndex,
                                    dependency.resourceName));
            ImGui::TableNextColumn();
            tableText(dependency.reason);
        }
        ImGui::EndTable();
    }

} // namespace

namespace asharia::editor {

    void drawRenderGraphSnapshotDetails(const EditorI18n& i18n,
                                        const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        const float height = detailHeight();
        drawAccessEventsList(i18n, snapshot, height);
        drawResourceList(i18n, snapshot, height);
        drawPassList(i18n, snapshot, height);
        drawDependencyList(i18n, snapshot, kDetailMinHeight);
    }

} // namespace asharia::editor

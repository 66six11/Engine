#include "panels/render_graph_snapshot_view.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

namespace {

    constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerV |
                                            ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    [[nodiscard]] const char* boolName(bool value) {
        return value ? "yes" : "no";
    }

    [[nodiscard]] std::string fallbackText(std::string_view value) {
        return value.empty() ? "-" : std::string{value};
    }

    [[nodiscard]] const char* viewportKindName(asharia::editor::EditorViewportKind kind) {
        switch (kind) {
        case asharia::editor::EditorViewportKind::Scene:
            return "Scene";
        case asharia::editor::EditorViewportKind::Game:
            return "Game";
        case asharia::editor::EditorViewportKind::Preview:
            return "Preview";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* resourceKindName(asharia::RenderGraphResourceKind kind) {
        switch (kind) {
        case asharia::RenderGraphResourceKind::Image:
            return "Image";
        case asharia::RenderGraphResourceKind::Buffer:
            return "Buffer";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* imageLifetimeName(asharia::RenderGraphImageLifetime lifetime) {
        switch (lifetime) {
        case asharia::RenderGraphImageLifetime::Imported:
            return "Imported";
        case asharia::RenderGraphImageLifetime::Transient:
            return "Transient";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* bufferLifetimeName(asharia::RenderGraphBufferLifetime lifetime) {
        switch (lifetime) {
        case asharia::RenderGraphBufferLifetime::Imported:
            return "Imported";
        case asharia::RenderGraphBufferLifetime::Transient:
            return "Transient";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* imageFormatName(asharia::RenderGraphImageFormat format) {
        switch (format) {
        case asharia::RenderGraphImageFormat::Undefined:
            return "Undefined";
        case asharia::RenderGraphImageFormat::B8G8R8A8Srgb:
            return "B8G8R8A8Srgb";
        case asharia::RenderGraphImageFormat::D32Sfloat:
            return "D32Sfloat";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* shaderStageName(asharia::RenderGraphShaderStage stage) {
        switch (stage) {
        case asharia::RenderGraphShaderStage::None:
            return "";
        case asharia::RenderGraphShaderStage::Fragment:
            return "fragment";
        case asharia::RenderGraphShaderStage::Compute:
            return "compute";
        }
        return "unknown";
    }

    [[nodiscard]] const char* imageStateName(asharia::RenderGraphImageState state) {
        switch (state) {
        case asharia::RenderGraphImageState::Undefined:
            return "Undefined";
        case asharia::RenderGraphImageState::ColorAttachment:
            return "ColorAttachment";
        case asharia::RenderGraphImageState::ShaderRead:
            return "ShaderRead";
        case asharia::RenderGraphImageState::DepthAttachmentRead:
            return "DepthAttachmentRead";
        case asharia::RenderGraphImageState::DepthAttachmentWrite:
            return "DepthAttachmentWrite";
        case asharia::RenderGraphImageState::DepthSampledRead:
            return "DepthSampledRead";
        case asharia::RenderGraphImageState::TransferDst:
            return "TransferDst";
        case asharia::RenderGraphImageState::Present:
            return "Present";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* bufferStateName(asharia::RenderGraphBufferState state) {
        switch (state) {
        case asharia::RenderGraphBufferState::Undefined:
            return "Undefined";
        case asharia::RenderGraphBufferState::TransferRead:
            return "TransferRead";
        case asharia::RenderGraphBufferState::TransferWrite:
            return "TransferWrite";
        case asharia::RenderGraphBufferState::HostRead:
            return "HostRead";
        case asharia::RenderGraphBufferState::ShaderRead:
            return "ShaderRead";
        case asharia::RenderGraphBufferState::StorageReadWrite:
            return "StorageReadWrite";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* slotAccessName(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::ColorWrite:
            return "ColorWrite";
        case asharia::RenderGraphSlotAccess::ShaderRead:
            return "ShaderRead";
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
            return "DepthAttachmentRead";
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
            return "DepthAttachmentWrite";
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
            return "DepthSampledRead";
        case asharia::RenderGraphSlotAccess::TransferWrite:
            return "TransferWrite";
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
            return "BufferShaderRead";
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return "BufferTransferRead";
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return "BufferTransferWrite";
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return "BufferStorageReadWrite";
        }
        return "Unknown";
    }

    [[nodiscard]] const char*
    transitionPhaseName(asharia::RenderGraphDiagnosticsTransitionPhase phase) {
        switch (phase) {
        case asharia::RenderGraphDiagnosticsTransitionPhase::BeforePass:
            return "Before";
        case asharia::RenderGraphDiagnosticsTransitionPhase::Final:
            return "Final";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string imageAccessName(asharia::RenderGraphImageAccess access) {
        std::string name{imageStateName(access.state)};
        const std::string_view stage{shaderStageName(access.shaderStage)};
        if (!stage.empty()) {
            name += "(";
            name += stage;
            name += ")";
        }
        return name;
    }

    [[nodiscard]] std::string bufferAccessName(asharia::RenderGraphBufferAccess access) {
        std::string name{bufferStateName(access.state)};
        const std::string_view stage{shaderStageName(access.shaderStage)};
        if (!stage.empty()) {
            name += "(";
            name += stage;
            name += ")";
        }
        return name;
    }

    [[nodiscard]] std::string resourceLabel(asharia::RenderGraphResourceKind kind,
                                            std::uint32_t index, std::string_view name) {
        std::string label = kind == asharia::RenderGraphResourceKind::Image ? "#" : "b#";
        label += std::to_string(index);
        label += " ";
        label += fallbackText(name);
        return label;
    }

    void tableText(std::string_view text) {
        const std::string copy{text};
        ImGui::TextUnformatted(copy.c_str());
    }

    void drawSummary(const asharia::editor::RenderGraphSnapshotViewDesc& desc,
                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        const std::string title = std::string{desc.sourceLabel} + ": view " +
                                  viewportKindName(desc.viewKind) + ", " +
                                  std::to_string(desc.requestedExtent.width) + "x" +
                                  std::to_string(desc.requestedExtent.height);
        ImGui::TextUnformatted(title.c_str());
        const std::string state = "Snapshot: " + fallbackText(desc.statusLabel) +
                                  ", submitted epoch " + std::to_string(desc.submittedFrameEpoch);
        ImGui::TextUnformatted(state.c_str());
        const std::string counts =
            "Passes " + std::to_string(snapshot.passes.size()) + " / Resources " +
            std::to_string(snapshot.resources.size()) + " / Access edges " +
            std::to_string(snapshot.accessEdges.size()) + " / Dependencies " +
            std::to_string(snapshot.dependencyEdges.size()) + " / Transitions " +
            std::to_string(snapshot.transitions.size());
        ImGui::TextUnformatted(counts.c_str());
    }

    void drawPassesTable(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        if (!ImGui::BeginTable("rg-passes", 8, kTableFlags, ImVec2{0.0F, 240.0F})) {
            return;
        }
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Decl");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Params");
        ImGui::TableSetupColumn("Commands");
        ImGui::TableSetupColumn("Transitions");
        ImGui::TableSetupColumn("Cullable");
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.passIndex));
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.declarationIndex));
            ImGui::TableNextColumn();
            tableText(pass.name);
            ImGui::TableNextColumn();
            tableText(fallbackText(pass.type));
            ImGui::TableNextColumn();
            tableText(fallbackText(pass.paramsType));
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.commandCount));
            ImGui::TableNextColumn();
            tableText(std::to_string(pass.imageTransitionCount + pass.bufferTransitionCount));
            ImGui::TableNextColumn();
            tableText(boolName(pass.allowCulling));
        }
        ImGui::EndTable();
    }

    void drawResourcesTable(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        if (!ImGui::BeginTable("rg-resources", 6, kTableFlags, ImVec2{0.0F, 240.0F})) {
            return;
        }
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Lifetime");
        ImGui::TableSetupColumn("Shape");
        ImGui::TableSetupColumn("Initial -> Final");
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsResourceNode& resource : snapshot.resources) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText(resourceKindName(resource.kind));
            ImGui::TableNextColumn();
            tableText(std::to_string(resource.resourceIndex));
            ImGui::TableNextColumn();
            tableText(resource.name);
            ImGui::TableNextColumn();
            tableText(resource.kind == asharia::RenderGraphResourceKind::Image
                          ? imageLifetimeName(resource.imageLifetime)
                          : bufferLifetimeName(resource.bufferLifetime));
            ImGui::TableNextColumn();
            if (resource.kind == asharia::RenderGraphResourceKind::Image) {
                const std::string shape = std::string{imageFormatName(resource.imageFormat)} + " " +
                                          std::to_string(resource.imageExtent.width) + "x" +
                                          std::to_string(resource.imageExtent.height);
                tableText(shape);
            } else {
                tableText(std::to_string(resource.bufferByteSize) + " bytes");
            }
            ImGui::TableNextColumn();
            if (resource.kind == asharia::RenderGraphResourceKind::Image) {
                tableText(imageAccessName(resource.imageInitialAccess) + " -> " +
                          imageAccessName(resource.imageFinalAccess));
            } else {
                tableText(bufferAccessName(resource.bufferInitialAccess) + " -> " +
                          bufferAccessName(resource.bufferFinalAccess));
            }
        }
        ImGui::EndTable();
    }

    void drawEdgesTable(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        if (!ImGui::BeginTable("rg-access-edges", 5, kTableFlags, ImVec2{0.0F, 170.0F})) {
            return;
        }
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Resource");
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Access");
        ImGui::TableSetupColumn("Stage");
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText("#" + std::to_string(edge.passIndex) + " " + edge.passName);
            ImGui::TableNextColumn();
            tableText(resourceLabel(edge.resourceKind, edge.resourceIndex, edge.resourceName));
            ImGui::TableNextColumn();
            tableText(edge.slotName);
            ImGui::TableNextColumn();
            tableText(slotAccessName(edge.access));
            ImGui::TableNextColumn();
            tableText(fallbackText(shaderStageName(edge.shaderStage)));
        }
        ImGui::EndTable();

        if (!ImGui::BeginTable("rg-dependencies", 4, kTableFlags, ImVec2{0.0F, 170.0F})) {
            return;
        }
        ImGui::TableSetupColumn("From");
        ImGui::TableSetupColumn("To");
        ImGui::TableSetupColumn("Resource");
        ImGui::TableSetupColumn("Reason");
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

    void drawTransitionsTable(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        if (!ImGui::BeginTable("rg-transitions", 5, kTableFlags, ImVec2{0.0F, 240.0F})) {
            return;
        }
        ImGui::TableSetupColumn("Phase");
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Resource");
        ImGui::TableSetupColumn("Old");
        ImGui::TableSetupColumn("New");
        ImGui::TableHeadersRow();
        for (const asharia::RenderGraphDiagnosticsTransition& transition : snapshot.transitions) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            tableText(transitionPhaseName(transition.phase));
            ImGui::TableNextColumn();
            tableText(transition.passName.empty() ? "-" : transition.passName);
            ImGui::TableNextColumn();
            tableText(resourceLabel(transition.resourceKind, transition.resourceIndex,
                                    transition.resourceName));
            ImGui::TableNextColumn();
            if (transition.resourceKind == asharia::RenderGraphResourceKind::Image) {
                tableText(imageAccessName(transition.oldImageAccess));
                ImGui::TableNextColumn();
                tableText(imageAccessName(transition.newImageAccess));
            } else {
                tableText(bufferAccessName(transition.oldBufferAccess));
                ImGui::TableNextColumn();
                tableText(bufferAccessName(transition.newBufferAccess));
            }
        }
        ImGui::EndTable();
    }

    void drawGraphList(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        if (snapshot.dependencyEdges.empty()) {
            ImGui::TextUnformatted("No dependency edges.");
            return;
        }

        if (!ImGui::BeginTable("rg-graph-list", 3, kTableFlags, ImVec2{0.0F, 240.0F})) {
            return;
        }
        ImGui::TableSetupColumn("Producer");
        ImGui::TableSetupColumn("Consumer");
        ImGui::TableSetupColumn("Resource");
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
        }
        ImGui::EndTable();
    }

} // namespace

namespace asharia::editor {

    void drawRenderGraphSnapshotView(const RenderGraphSnapshotViewDesc& desc,
                                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        drawSummary(desc, snapshot);
        ImGui::Separator();

        if (ImGui::BeginTabBar("rg-view-tabs")) {
            if (ImGui::BeginTabItem("Passes")) {
                drawPassesTable(snapshot);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Resources")) {
                drawResourcesTable(snapshot);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Edges")) {
                drawEdgesTable(snapshot);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Transitions")) {
                drawTransitionsTable(snapshot);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Graph")) {
                drawGraphList(snapshot);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

} // namespace asharia::editor

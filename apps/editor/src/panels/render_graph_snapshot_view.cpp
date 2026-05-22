#include "panels/render_graph_snapshot_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"

namespace {

    constexpr float kTimelinePassColumnWidth = 24.0F;
    constexpr float kTimelineResourceColumnWidth = 220.0F;
    constexpr float kTimelineHeaderAngleRadians = -1.08F;
    constexpr float kTimelineHeaderMinHeight = 96.0F;
    constexpr float kTimelineHeaderMaxHeight = 180.0F;
    constexpr float kTimelineRowHeight = 22.0F;
    constexpr float kTimelineMinHeight = 180.0F;
    constexpr float kDetailMinHeight = 120.0F;
    constexpr float kDetailMaxHeight = 180.0F;

    constexpr ImGuiTableFlags kDetailTableFlags =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    struct AccessCell {
        bool read{};
        bool write{};
        std::uint32_t accessCount{};
        std::string detail;
    };

    struct TimelineHitAxis {
        float position{};
        float scroll{};
        float origin{};
        float extent{};
        int count{};
    };

    struct TimelineCanvas {
        ImDrawList* drawList{};
        ImVec2 origin;
        ImVec2 clipMax;
        float scrollX{};
        float scrollY{};
        float headerHeight{};
        int passCount{};
        int resourceCount{};
        ImU32 borderColor{};
        ImU32 headerColor{};
        ImU32 rowAltColor{};
        ImU32 textColor{};
        ImU32 textDisabledColor{};
    };

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
        case asharia::RenderGraphImageState::TransferSrc:
            return "TransferSrc";
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
        case asharia::RenderGraphSlotAccess::TransferRead:
            return "TransferRead";
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

    [[nodiscard]] const char* accessRoleName(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::ColorWrite:
            return "Color";
        case asharia::RenderGraphSlotAccess::ShaderRead:
            return "Sample";
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
            return "Depth R";
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
            return "Depth W";
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
            return "Depth Sample";
        case asharia::RenderGraphSlotAccess::TransferRead:
            return "Copy Src";
        case asharia::RenderGraphSlotAccess::TransferWrite:
            return "Copy Dst";
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
            return "Buffer R";
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return "Buffer Src";
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return "Buffer Dst";
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return "Storage RW";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* accessDirectionName(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return "Read/Write";
        case asharia::RenderGraphSlotAccess::ColorWrite:
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
        case asharia::RenderGraphSlotAccess::TransferWrite:
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return "Write";
        case asharia::RenderGraphSlotAccess::ShaderRead:
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
        case asharia::RenderGraphSlotAccess::TransferRead:
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return "Read";
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

    [[nodiscard]] std::string passHeaderLabel(const asharia::RenderGraphDiagnosticsPassNode& pass) {
        std::string label = "#" + std::to_string(pass.passIndex);
        if (!pass.name.empty()) {
            label += " ";
            label += pass.name;
        }
        return label;
    }

    void tableText(std::string_view text) {
        const std::string copy{text};
        ImGui::TextUnformatted(copy.c_str());
    }

    void coloredText(const ImVec4& color, std::string_view text) {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        tableText(text);
        ImGui::PopStyleColor();
    }

    void disabledText(std::string_view text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        tableText(text);
        ImGui::PopStyleColor();
    }

    void tooltipText(std::string_view text) {
        const std::string copy{text};
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(copy.c_str());
        ImGui::EndTooltip();
    }

    void drawRotatedText(std::string_view text, const ImVec2& origin, ImU32 color) {
        const std::string copy{text};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const int vertexStart = drawList->VtxBuffer.Size;
        drawList->AddText(origin, color, copy.c_str());
        const int vertexEnd = drawList->VtxBuffer.Size;

        const float sinAngle = std::sin(kTimelineHeaderAngleRadians);
        const float cosAngle = std::cos(kTimelineHeaderAngleRadians);
        for (int vertexIndex = vertexStart; vertexIndex < vertexEnd; ++vertexIndex) {
            ImVec2& position = drawList->VtxBuffer[vertexIndex].pos;
            const float offsetX = position.x - origin.x;
            const float offsetY = position.y - origin.y;
            position.x = origin.x + (offsetX * cosAngle) - (offsetY * sinAngle);
            position.y = origin.y + (offsetX * sinAngle) + (offsetY * cosAngle);
        }
    }

    [[nodiscard]] bool accessReads(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::ShaderRead:
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
        case asharia::RenderGraphSlotAccess::TransferRead:
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return true;
        case asharia::RenderGraphSlotAccess::ColorWrite:
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
        case asharia::RenderGraphSlotAccess::TransferWrite:
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return false;
        }
        return false;
    }

    [[nodiscard]] bool accessWrites(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::ColorWrite:
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
        case asharia::RenderGraphSlotAccess::TransferWrite:
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return true;
        case asharia::RenderGraphSlotAccess::ShaderRead:
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
        case asharia::RenderGraphSlotAccess::TransferRead:
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return false;
        }
        return false;
    }

    [[nodiscard]] AccessCell
    accessCellFor(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                  const asharia::RenderGraphDiagnosticsResourceNode& resource,
                  const asharia::RenderGraphDiagnosticsPassNode& pass) {
        AccessCell cell;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passIndex != pass.passIndex || edge.resourceKind != resource.kind ||
                edge.resourceIndex != resource.resourceIndex) {
                continue;
            }

            cell.read = cell.read || accessReads(edge.access);
            cell.write = cell.write || accessWrites(edge.access);
            ++cell.accessCount;

            if (!cell.detail.empty()) {
                cell.detail += "\n";
            }
            cell.detail += edge.slotName;
            cell.detail += ": ";
            cell.detail += accessDirectionName(edge.access);
            cell.detail += " ";
            cell.detail += accessRoleName(edge.access);
            cell.detail += " / ";
            cell.detail += slotAccessName(edge.access);
            const std::string_view stage{shaderStageName(edge.shaderStage)};
            if (!stage.empty()) {
                cell.detail += "(";
                cell.detail += stage;
                cell.detail += ")";
            }
        }
        return cell;
    }

    [[nodiscard]] std::string accessCellLabel(const AccessCell& cell) {
        if (cell.accessCount == 0U) {
            return "-";
        }
        if (cell.read && cell.write) {
            return "rw";
        }
        if (cell.write) {
            return "w";
        }
        return "r";
    }

    [[nodiscard]] ImU32 accessCellColor(const AccessCell& cell) {
        if (cell.read && cell.write) {
            return IM_COL32(186, 117, 42, 160);
        }
        if (cell.write) {
            return IM_COL32(164, 54, 45, 150);
        }
        if (cell.read) {
            return IM_COL32(48, 126, 73, 150);
        }
        return IM_COL32(50, 50, 50, 70);
    }

    [[nodiscard]] std::string
    resourceShape(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        if (resource.kind == asharia::RenderGraphResourceKind::Image) {
            return std::string{imageFormatName(resource.imageFormat)} + " " +
                   std::to_string(resource.imageExtent.width) + "x" +
                   std::to_string(resource.imageExtent.height);
        }
        return std::to_string(resource.bufferByteSize) + " bytes";
    }

    [[nodiscard]] std::string
    resourceLifetime(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        return resource.kind == asharia::RenderGraphResourceKind::Image
                   ? imageLifetimeName(resource.imageLifetime)
                   : bufferLifetimeName(resource.bufferLifetime);
    }

    [[nodiscard]] std::string
    resourceAccessRange(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        if (resource.kind == asharia::RenderGraphResourceKind::Image) {
            return imageAccessName(resource.imageInitialAccess) + " -> " +
                   imageAccessName(resource.imageFinalAccess);
        }
        return bufferAccessName(resource.bufferInitialAccess) + " -> " +
               bufferAccessName(resource.bufferFinalAccess);
    }

    [[nodiscard]] std::string
    unityResourcePrefix(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        std::string prefix = resource.kind == asharia::RenderGraphResourceKind::Image ? "T " : "B ";
        if ((resource.kind == asharia::RenderGraphResourceKind::Image &&
             resource.imageLifetime == asharia::RenderGraphImageLifetime::Imported) ||
            (resource.kind == asharia::RenderGraphResourceKind::Buffer &&
             resource.bufferLifetime == asharia::RenderGraphBufferLifetime::Imported)) {
            prefix += "<- ";
        }
        return prefix;
    }

    [[nodiscard]] float detailHeight() {
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        if (availableHeight <= 0.0F) {
            return 150.0F;
        }
        return std::clamp(availableHeight * 0.48F, kDetailMinHeight, kDetailMaxHeight);
    }

    void drawSummary(const asharia::editor::RenderGraphSnapshotViewDesc& desc,
                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        const std::string title = std::string{desc.sourceLabel} + ": view " +
                                  viewportKindName(desc.viewKind) + ", " +
                                  std::to_string(desc.requestedExtent.width) + "x" +
                                  std::to_string(desc.requestedExtent.height);
        ImGui::TextUnformatted(title.c_str());
        const std::string state = std::string{desc.i18n.text("renderGraph.snapshot")} + ": " +
                                  fallbackText(desc.statusLabel) + ", " +
                                  std::string{desc.i18n.text("renderGraph.submittedEpoch")} + " " +
                                  std::to_string(desc.submittedFrameEpoch);
        ImGui::TextUnformatted(state.c_str());
        const std::string counts = std::string{desc.i18n.text("renderGraph.passes")} + " " +
                                   std::to_string(snapshot.passes.size()) + " / " +
                                   std::string{desc.i18n.text("renderGraph.resources")} + " " +
                                   std::to_string(snapshot.resources.size()) + " / " +
                                   std::string{desc.i18n.text("renderGraph.accessEdges")} + " " +
                                   std::to_string(snapshot.accessEdges.size()) + " / " +
                                   std::string{desc.i18n.text("renderGraph.dependencies")} + " " +
                                   std::to_string(snapshot.dependencyEdges.size()) + " / " +
                                   std::string{desc.i18n.text("renderGraph.transitions")} + " " +
                                   std::to_string(snapshot.transitions.size());
        ImGui::TextUnformatted(counts.c_str());
    }

    void drawAccessLegend(const asharia::editor::EditorI18n& i18n) {
        tableText(i18n.text("renderGraph.colors"));
        ImGui::SameLine();
        coloredText(ImVec4{0.38F, 0.90F, 0.50F, 1.0F}, "r");
        ImGui::SameLine();
        tableText(i18n.text("renderGraph.read"));
        ImGui::SameLine();
        coloredText(ImVec4{1.0F, 0.42F, 0.36F, 1.0F}, "w");
        ImGui::SameLine();
        tableText(i18n.text("renderGraph.write"));
        ImGui::SameLine();
        coloredText(ImVec4{1.0F, 0.72F, 0.34F, 1.0F}, "rw");
        ImGui::SameLine();
        tableText(i18n.text("renderGraph.readWrite"));
        ImGui::SameLine();
        disabledText("hover cells for slot/use details; T texture, B buffer, <- imported");
    }

    [[nodiscard]] float
    timelineHeaderHeight(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        float maxLabelWidth = 0.0F;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            const std::string label = passHeaderLabel(pass);
            maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(label.c_str()).x);
        }
        const float verticalTextSpan =
            maxLabelWidth * std::sin(std::abs(kTimelineHeaderAngleRadians));
        return std::clamp(verticalTextSpan + ImGui::GetTextLineHeightWithSpacing(),
                          kTimelineHeaderMinHeight, kTimelineHeaderMaxHeight);
    }

    [[nodiscard]] float
    timelineHeaderOverhang(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        float maxLabelWidth = 0.0F;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            const std::string label = passHeaderLabel(pass);
            maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(label.c_str()).x);
        }
        return (maxLabelWidth * std::cos(std::abs(kTimelineHeaderAngleRadians))) +
               ImGui::GetTextLineHeightWithSpacing();
    }

    void drawResourceTooltip(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        std::string tooltip = resourceShape(resource);
        tooltip += "\n";
        tooltip += resourceLifetime(resource);
        tooltip += "\n";
        tooltip += resourceAccessRange(resource);
        tooltipText(tooltip);
    }

    void drawAccessCellTooltip(const asharia::RenderGraphDiagnosticsResourceNode& resource,
                               const asharia::RenderGraphDiagnosticsPassNode& pass,
                               const AccessCell& cell) {
        const std::string passLabel = passHeaderLabel(pass);
        const std::string resourceName =
            resourceLabel(resource.kind, resource.resourceIndex, resource.name);
        const std::string detail = cell.detail.empty() ? "No access" : cell.detail;
        std::string tooltip = passLabel;
        tooltip += "\n";
        tooltip += resourceName;
        tooltip += "\n";
        tooltip += detail;
        tooltipText(tooltip);
    }

    [[nodiscard]] bool pointInRect(const ImVec2& point, const ImVec2& min, const ImVec2& max) {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }

    [[nodiscard]] int hoveredTimelineIndex(const TimelineHitAxis& axis) {
        const int index =
            static_cast<int>(std::floor((axis.position - axis.origin + axis.scroll) / axis.extent));
        return index >= 0 && index < axis.count ? index : -1;
    }

    void drawTimelineHeader(const asharia::editor::EditorI18n& i18n,
                            const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                            const TimelineCanvas& canvas) {
        const ImVec2 headerMax{canvas.clipMax.x, canvas.origin.y + canvas.headerHeight};
        canvas.drawList->AddRectFilled(canvas.origin, headerMax, canvas.headerColor);
        canvas.drawList->AddRectFilled(canvas.origin,
                                       ImVec2{canvas.origin.x + kTimelineResourceColumnWidth,
                                              canvas.origin.y + canvas.headerHeight},
                                       canvas.headerColor);
        const std::string resourceText{i18n.text("renderGraph.resource")};
        canvas.drawList->AddText(
            ImVec2{canvas.origin.x + 6.0F, canvas.origin.y + canvas.headerHeight - 24.0F},
            canvas.textColor, resourceText.c_str());

        canvas.drawList->PushClipRect(
            ImVec2{canvas.origin.x + kTimelineResourceColumnWidth, canvas.origin.y},
            ImVec2{canvas.clipMax.x, canvas.origin.y + canvas.headerHeight}, true);
        for (int passPosition = 0; passPosition < canvas.passCount; ++passPosition) {
            const asharia::RenderGraphDiagnosticsPassNode& pass =
                snapshot.passes[static_cast<std::size_t>(passPosition)];
            const float headerX = canvas.origin.x + kTimelineResourceColumnWidth +
                                  (static_cast<float>(passPosition) * kTimelinePassColumnWidth) -
                                  canvas.scrollX;
            const std::string label = passHeaderLabel(pass);
            drawRotatedText(label,
                            ImVec2{headerX + 3.0F, canvas.origin.y + canvas.headerHeight - 6.0F},
                            canvas.textColor);
        }
        canvas.drawList->PopClipRect();
        canvas.drawList->AddLine(ImVec2{canvas.origin.x, canvas.origin.y + canvas.headerHeight},
                                 ImVec2{canvas.clipMax.x, canvas.origin.y + canvas.headerHeight},
                                 canvas.borderColor);
    }

    void drawTimelineResourceLabel(const TimelineCanvas& canvas,
                                   const asharia::RenderGraphDiagnosticsResourceNode& resource,
                                   float rowY) {
        const std::string label =
            unityResourcePrefix(resource) +
            resourceLabel(resource.kind, resource.resourceIndex, resource.name);
        canvas.drawList->PushClipRect(
            ImVec2{canvas.origin.x, canvas.origin.y + canvas.headerHeight},
            ImVec2{canvas.origin.x + kTimelineResourceColumnWidth, canvas.clipMax.y}, true);
        canvas.drawList->AddText(ImVec2{canvas.origin.x + 6.0F, rowY + 3.0F}, canvas.textColor,
                                 label.c_str());
        canvas.drawList->PopClipRect();
    }

    void drawTimelineCellsForResource(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                      const TimelineCanvas& canvas,
                                      const asharia::RenderGraphDiagnosticsResourceNode& resource,
                                      float rowY) {
        canvas.drawList->PushClipRect(ImVec2{canvas.origin.x + kTimelineResourceColumnWidth,
                                             canvas.origin.y + canvas.headerHeight},
                                      canvas.clipMax, true);
        for (int passPosition = 0; passPosition < canvas.passCount; ++passPosition) {
            const asharia::RenderGraphDiagnosticsPassNode& pass =
                snapshot.passes[static_cast<std::size_t>(passPosition)];
            const float cellX = canvas.origin.x + kTimelineResourceColumnWidth +
                                (static_cast<float>(passPosition) * kTimelinePassColumnWidth) -
                                canvas.scrollX;
            const ImVec2 cellMin{cellX + 1.0F, rowY + 1.0F};
            const ImVec2 cellMax{cellX + kTimelinePassColumnWidth - 1.0F,
                                 rowY + kTimelineRowHeight - 1.0F};
            if (cellMax.x < canvas.origin.x + kTimelineResourceColumnWidth ||
                cellMin.x > canvas.clipMax.x) {
                continue;
            }

            const AccessCell cell = accessCellFor(snapshot, resource, pass);
            canvas.drawList->AddRectFilled(cellMin, cellMax, accessCellColor(cell));
            const std::string cellLabel = accessCellLabel(cell);
            const ImVec2 labelSize = ImGui::CalcTextSize(cellLabel.c_str());
            const ImU32 labelColor =
                cell.accessCount == 0U ? canvas.textDisabledColor : canvas.textColor;
            canvas.drawList->AddText(
                ImVec2{cellX + ((kTimelinePassColumnWidth - labelSize.x) * 0.5F),
                       rowY + ((kTimelineRowHeight - labelSize.y) * 0.5F)},
                labelColor, cellLabel.c_str());
        }
        canvas.drawList->PopClipRect();
    }

    void drawTimelineRows(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                          const TimelineCanvas& canvas) {
        canvas.drawList->PushClipRect(
            ImVec2{canvas.origin.x, canvas.origin.y + canvas.headerHeight}, canvas.clipMax, true);
        for (int resourceIndex = 0; resourceIndex < canvas.resourceCount; ++resourceIndex) {
            const asharia::RenderGraphDiagnosticsResourceNode& resource =
                snapshot.resources[static_cast<std::size_t>(resourceIndex)];
            const float rowY = canvas.origin.y + canvas.headerHeight +
                               (static_cast<float>(resourceIndex) * kTimelineRowHeight) -
                               canvas.scrollY;
            const ImVec2 rowMin{canvas.origin.x, rowY};
            const ImVec2 rowMax{canvas.clipMax.x, rowY + kTimelineRowHeight};
            if (rowMax.y < canvas.origin.y + canvas.headerHeight || rowMin.y > canvas.clipMax.y) {
                continue;
            }
            if ((resourceIndex % 2) != 0) {
                canvas.drawList->AddRectFilled(rowMin, rowMax, canvas.rowAltColor);
            }

            drawTimelineResourceLabel(canvas, resource, rowY);
            drawTimelineCellsForResource(snapshot, canvas, resource, rowY);
        }
        canvas.drawList->PopClipRect();
    }

    void drawTimelineGrid(const TimelineCanvas& canvas) {
        for (int passPosition = 0; passPosition <= canvas.passCount; ++passPosition) {
            const float lineX = canvas.origin.x + kTimelineResourceColumnWidth +
                                (static_cast<float>(passPosition) * kTimelinePassColumnWidth) -
                                canvas.scrollX;
            if (lineX >= canvas.origin.x + kTimelineResourceColumnWidth &&
                lineX <= canvas.clipMax.x) {
                canvas.drawList->AddLine(ImVec2{lineX, canvas.origin.y + canvas.headerHeight},
                                         ImVec2{lineX, canvas.clipMax.y}, canvas.borderColor);
            }
        }
        canvas.drawList->AddLine(
            ImVec2{canvas.origin.x + kTimelineResourceColumnWidth,
                   canvas.origin.y + canvas.headerHeight},
            ImVec2{canvas.origin.x + kTimelineResourceColumnWidth, canvas.clipMax.y},
            canvas.borderColor);
    }

    void drawTimelineHoverTooltips(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                   const TimelineCanvas& canvas) {
        if (!ImGui::IsWindowHovered()) {
            return;
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float matrixX = canvas.origin.x + kTimelineResourceColumnWidth;
        if (pointInRect(mouse, ImVec2{matrixX, canvas.origin.y},
                        ImVec2{canvas.clipMax.x, canvas.origin.y + canvas.headerHeight})) {
            const int passPosition =
                hoveredTimelineIndex(TimelineHitAxis{.position = mouse.x,
                                                     .scroll = canvas.scrollX,
                                                     .origin = matrixX,
                                                     .extent = kTimelinePassColumnWidth,
                                                     .count = canvas.passCount});
            if (passPosition >= 0) {
                tooltipText(
                    passHeaderLabel(snapshot.passes[static_cast<std::size_t>(passPosition)]));
            }
            return;
        }

        if (!pointInRect(mouse, ImVec2{canvas.origin.x, canvas.origin.y + canvas.headerHeight},
                         canvas.clipMax)) {
            return;
        }

        const int resourcePosition =
            hoveredTimelineIndex(TimelineHitAxis{.position = mouse.y,
                                                 .scroll = canvas.scrollY,
                                                 .origin = canvas.origin.y + canvas.headerHeight,
                                                 .extent = kTimelineRowHeight,
                                                 .count = canvas.resourceCount});
        if (resourcePosition < 0) {
            return;
        }

        const asharia::RenderGraphDiagnosticsResourceNode& resource =
            snapshot.resources[static_cast<std::size_t>(resourcePosition)];
        if (mouse.x < matrixX) {
            drawResourceTooltip(resource);
            return;
        }

        const int passPosition =
            hoveredTimelineIndex(TimelineHitAxis{.position = mouse.x,
                                                 .scroll = canvas.scrollX,
                                                 .origin = matrixX,
                                                 .extent = kTimelinePassColumnWidth,
                                                 .count = canvas.passCount});
        if (passPosition < 0) {
            return;
        }

        const asharia::RenderGraphDiagnosticsPassNode& pass =
            snapshot.passes[static_cast<std::size_t>(passPosition)];
        drawAccessCellTooltip(resource, pass, accessCellFor(snapshot, resource, pass));
    }

    void drawAccessTimeline(const asharia::editor::EditorI18n& i18n,
                            const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        drawAccessLegend(i18n);

        if (snapshot.resources.empty() || snapshot.passes.empty()) {
            disabledText(i18n.text("renderGraph.noTimeline"));
            return;
        }

        const int passCount = static_cast<int>(snapshot.passes.size());
        const int resourceCount = static_cast<int>(snapshot.resources.size());
        const float headerHeight = timelineHeaderHeight(snapshot);
        const float canvasWidth = kTimelineResourceColumnWidth +
                                  (static_cast<float>(passCount) * kTimelinePassColumnWidth) +
                                  timelineHeaderOverhang(snapshot);
        const float canvasHeight =
            headerHeight + (static_cast<float>(resourceCount) * kTimelineRowHeight);
        const float visibleWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
        const bool needsHorizontalScrollbar = canvasWidth > visibleWidth;
        const float scrollbarAllowance =
            needsHorizontalScrollbar ? ImGui::GetStyle().ScrollbarSize : 0.0F;
        const float childHeight =
            std::max(canvasHeight + scrollbarAllowance, kTimelineMinHeight);
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (!ImGui::BeginChild("rg-access-matrix-canvas", ImVec2{0.0F, childHeight},
                               ImGuiChildFlags_None, windowFlags)) {
            ImGui::EndChild();
            return;
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 visibleSize = ImGui::GetContentRegionAvail();
        const TimelineCanvas canvas{
            .drawList = ImGui::GetWindowDrawList(),
            .origin = origin,
            .clipMax = ImVec2{origin.x + visibleSize.x, origin.y + visibleSize.y},
            .scrollX = ImGui::GetScrollX(),
            .scrollY = 0.0F,
            .headerHeight = headerHeight,
            .passCount = passCount,
            .resourceCount = resourceCount,
            .borderColor = ImGui::GetColorU32(ImGuiCol_Border),
            .headerColor = ImGui::GetColorU32(ImGuiCol_TableHeaderBg),
            .rowAltColor = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt),
            .textColor = ImGui::GetColorU32(ImGuiCol_Text),
            .textDisabledColor = ImGui::GetColorU32(ImGuiCol_TextDisabled),
        };
        drawTimelineRows(snapshot, canvas);
        drawTimelineGrid(canvas);
        drawTimelineHeader(i18n, snapshot, canvas);
        drawTimelineHoverTooltips(snapshot, canvas);

        ImGui::Dummy(ImVec2{canvasWidth, canvasHeight});
        ImGui::EndChild();
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

    void drawDetails(const asharia::editor::EditorI18n& i18n,
                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        const float height = detailHeight();
        drawAccessEventsList(i18n, snapshot, height);
        drawResourceList(i18n, snapshot, height);
        drawPassList(i18n, snapshot, height);
        drawDependencyList(i18n, snapshot, kDetailMinHeight);
    }

} // namespace

namespace asharia::editor {

    void drawRenderGraphSnapshotView(const RenderGraphSnapshotViewDesc& desc,
                                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        drawSummary(desc, snapshot);
        ImGui::Separator();
        drawAccessTimeline(desc.i18n, snapshot);
        ImGui::Separator();
        drawDetails(desc.i18n, snapshot);
    }

} // namespace asharia::editor

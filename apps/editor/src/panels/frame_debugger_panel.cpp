#include "panels/frame_debugger_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <imgui.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "panels/render_graph_snapshot_view.hpp"

namespace asharia::editor {
    namespace {

        constexpr float kEventListMinWidth = 220.0F;
        constexpr float kEventListMaxWidth = 360.0F;
        constexpr float kEventListNarrowWidth = 160.0F;
        constexpr float kEventListWidthRatio = 0.36F;
        constexpr float kDetailsPaneMinWidth = 260.0F;
        constexpr float kPreviewImageMaxHeight = 220.0F;

        struct FrameDebuggerPanelContext {
            const EditorFrameUiContext* ui{};
            EditorFrameDebugger* frameDebugger{};
        };

        void textUnformatted(std::string_view text) {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void disabledText(std::string_view text) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            textUnformatted(text);
            ImGui::PopStyleColor();
        }

        [[nodiscard]] const char* imageFormatName(asharia::RenderGraphImageFormat format) {
            switch (format) {
            case asharia::RenderGraphImageFormat::B8G8R8A8Srgb:
                return "B8G8R8A8Srgb";
            case asharia::RenderGraphImageFormat::D32Sfloat:
                return "D32Sfloat";
            case asharia::RenderGraphImageFormat::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] std::string
        imageResourceLabel(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
            return "#" + std::to_string(resource.resourceIndex) + " " + resource.name + " " +
                   imageFormatName(resource.imageFormat) + " " +
                   std::to_string(resource.imageExtent.width) + "x" +
                   std::to_string(resource.imageExtent.height);
        }

        [[nodiscard]] std::string
        passEventLabel(const asharia::RenderGraphDiagnosticsPassNode& pass) {
            std::string label = "#" + std::to_string(pass.passIndex);
            if (!pass.name.empty()) {
                label += " ";
                label += pass.name;
            }
            if (!pass.type.empty()) {
                label += " / ";
                label += pass.type;
            }
            return label;
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

        [[nodiscard]] const char* commandKindName(asharia::RenderGraphCommandKind kind) {
            switch (kind) {
            case asharia::RenderGraphCommandKind::SetShader:
                return "SetShader";
            case asharia::RenderGraphCommandKind::SetTexture:
                return "SetTexture";
            case asharia::RenderGraphCommandKind::SetFloat:
                return "SetFloat";
            case asharia::RenderGraphCommandKind::SetInt:
                return "SetInt";
            case asharia::RenderGraphCommandKind::SetVec4:
                return "SetVec4";
            case asharia::RenderGraphCommandKind::DrawFullscreenTriangle:
                return "DrawFullscreenTriangle";
            case asharia::RenderGraphCommandKind::ClearColor:
                return "ClearColor";
            case asharia::RenderGraphCommandKind::FillBuffer:
                return "FillBuffer";
            case asharia::RenderGraphCommandKind::CopyImage:
                return "CopyImage";
            case asharia::RenderGraphCommandKind::CopyBuffer:
                return "CopyBuffer";
            case asharia::RenderGraphCommandKind::CopyBufferToImage:
                return "CopyBufferToImage";
            case asharia::RenderGraphCommandKind::CopyImageToBuffer:
                return "CopyImageToBuffer";
            case asharia::RenderGraphCommandKind::Dispatch:
                return "Dispatch";
            }
            return "Unknown";
        }

        [[nodiscard]] const char*
        executionEventKindName(asharia::BasicRenderViewExecutionEventKind kind) {
            switch (kind) {
            case asharia::BasicRenderViewExecutionEventKind::BeginPass:
                return "BeginPass";
            case asharia::BasicRenderViewExecutionEventKind::EndPass:
                return "EndPass";
            case asharia::BasicRenderViewExecutionEventKind::ClearColor:
                return "ClearColor";
            case asharia::BasicRenderViewExecutionEventKind::Draw:
                return "Draw";
            case asharia::BasicRenderViewExecutionEventKind::DrawIndexed:
                return "DrawIndexed";
            case asharia::BasicRenderViewExecutionEventKind::DrawFullscreenTriangle:
                return "DrawFullscreenTriangle";
            case asharia::BasicRenderViewExecutionEventKind::Dispatch:
                return "Dispatch";
            case asharia::BasicRenderViewExecutionEventKind::CopyImage:
                return "CopyImage";
            case asharia::BasicRenderViewExecutionEventKind::RenderViewInput:
                return "RenderViewInput";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string
        executionEventLabel(const asharia::BasicRenderViewExecutionEvent& event) {
            std::string label =
                "#" + std::to_string(event.id.value) + " " + executionEventKindName(event.kind);
            if (!event.label.empty()) {
                label += " / ";
                label += event.label;
            }
            return label;
        }

        [[nodiscard]] bool
        previewableExecutionEventKind(asharia::BasicRenderViewExecutionEventKind kind) {
            return kind != asharia::BasicRenderViewExecutionEventKind::BeginPass &&
                   kind != asharia::BasicRenderViewExecutionEventKind::EndPass;
        }

        [[nodiscard]] const asharia::RenderGraphDiagnosticsPassNode*
        selectedPassNode(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                         const EditorFrameDebugPreview& preview) {
            if (!preview.selectedPassIndex) {
                return nullptr;
            }
            for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
                if (pass.passIndex == *preview.selectedPassIndex) {
                    return &pass;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        selectedExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics,
                               const EditorFrameDebugPreview& preview) {
            if (!preview.selectedExecutionEventId) {
                return nullptr;
            }
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (event.id == *preview.selectedExecutionEventId) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>
        capturedImageResources(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            std::vector<const asharia::RenderGraphDiagnosticsResourceNode*> images;
            for (const asharia::RenderGraphDiagnosticsResourceNode& resource : snapshot.resources) {
                if (resource.kind == asharia::RenderGraphResourceKind::Image) {
                    images.push_back(&resource);
                }
            }
            return images;
        }

        void drawPassSelectable(FrameDebuggerPanelContext& context,
                                const asharia::RenderGraphDiagnosticsPassNode& pass,
                                const EditorFrameDebugPreview& preview) {
            const bool selected = preview.selectedPassIndex &&
                                  *preview.selectedPassIndex == pass.passIndex &&
                                  !preview.selectedExecutionEventId;
            const std::string label = passEventLabel(pass);
            if (ImGui::Selectable(label.c_str(), selected)) {
                static_cast<void>(context.frameDebugger->selectReplayPass(pass.passIndex));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        [[nodiscard]] bool
        drawExecutionEventsForPass(FrameDebuggerPanelContext& context,
                                   std::span<const asharia::BasicRenderViewExecutionEvent> events,
                                   const asharia::RenderGraphDiagnosticsPassNode& pass,
                                   const EditorFrameDebugPreview& preview) {
            bool listed = false;
            for (const asharia::BasicRenderViewExecutionEvent& event : events) {
                if (event.passIndex != pass.passIndex) {
                    continue;
                }
                listed = true;
                const bool selected = preview.selectedExecutionEventId &&
                                      *preview.selectedExecutionEventId == event.id;
                const std::string label = executionEventLabel(event);
                if (!previewableExecutionEventKind(event.kind)) {
                    disabledText(label);
                    continue;
                }
                if (ImGui::Selectable(label.c_str(), selected)) {
                    static_cast<void>(context.frameDebugger->selectReplayEvent(event.id));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            return listed;
        }

        void drawCommandSummariesForPass(
            FrameDebuggerPanelContext& context,
            std::span<const asharia::RenderGraphDiagnosticsCommandNode> commands,
            const asharia::RenderGraphDiagnosticsPassNode& pass) {
            for (const asharia::RenderGraphDiagnosticsCommandNode& command : commands) {
                if (command.passIndex != pass.passIndex) {
                    continue;
                }
                const std::string label =
                    std::to_string(command.commandIndex) + " " + commandKindName(command.kind);
                if (ImGui::Selectable(label.c_str(), false)) {
                    static_cast<void>(context.frameDebugger->selectReplayPass(pass.passIndex));
                }
            }
        }

        void drawPassEventList(FrameDebuggerPanelContext& context,
                               const asharia::BasicRenderViewDiagnostics& diagnostics,
                               const EditorFrameDebugPreview& preview) {
            const asharia::RenderGraphDiagnosticsSnapshot& snapshot = diagnostics.renderGraph;
            if (snapshot.passes.empty()) {
                textUnformatted(context.ui->i18n.text(EditorI18nTextQuery{
                    .key = "frameDebug.passEmpty",
                    .fallback = "Pass: -",
                }));
                return;
            }

            for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
                drawPassSelectable(context, pass, preview);
                ImGui::Indent(14.0F);
                const bool listedEvents =
                    drawExecutionEventsForPass(context, diagnostics.executionEvents, pass, preview);
                if (!listedEvents) {
                    drawCommandSummariesForPass(context, snapshot.commands, pass);
                }
                ImGui::Unindent(14.0F);
            }
        }

        void drawImageSelector(
            FrameDebuggerPanelContext& context,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            const EditorI18n& i18n = context.ui->i18n;
            if (imageResources.empty()) {
                textUnformatted(i18n.text("frameDebug.imageEmpty"));
                return;
            }

            std::string selectedLabel{i18n.text("frameDebug.noImage")};
            if (preview.selectedImageResourceIndex) {
                for (const asharia::RenderGraphDiagnosticsResourceNode* resource : imageResources) {
                    if (resource->resourceIndex == *preview.selectedImageResourceIndex) {
                        selectedLabel = imageResourceLabel(*resource);
                        break;
                    }
                }
            }

            ImGui::SetNextItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x));
            const std::string imageComboLabel = i18n.label(EditorI18nLabelDesc{
                .key = "frameDebug.image",
                .stableId = "frame-debug-image",
                .fallback = "Image",
            });
            if (ImGui::BeginCombo(imageComboLabel.c_str(), selectedLabel.c_str())) {
                for (const asharia::RenderGraphDiagnosticsResourceNode* resource : imageResources) {
                    const bool selected =
                        preview.selectedImageResourceIndex &&
                        *preview.selectedImageResourceIndex == resource->resourceIndex;
                    const std::string label = imageResourceLabel(*resource);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        static_cast<void>(context.frameDebugger->selectPreviewImageResource(
                            resource->resourceIndex));
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void drawPreviewTexture(const EditorFrameDebugPreview& preview, float maxHeight) {
            const bool hasTexture = hasEditorViewportTexture(preview.texture);
            if (hasTexture) {
                const float maxWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
                const float width =
                    std::min(maxWidth, static_cast<float>(preview.texture.extent.width));
                const float aspect = static_cast<float>(preview.texture.extent.height) /
                                     static_cast<float>(std::max(1U, preview.texture.extent.width));
                float height = std::max(1.0F, width * aspect);
                float imageWidth = width;
                if (height > maxHeight) {
                    const float scale = maxHeight / height;
                    height = maxHeight;
                    imageWidth = std::max(1.0F, imageWidth * scale);
                }
                ImGui::Image(static_cast<ImTextureID>(preview.texture.textureId),
                             ImVec2{imageWidth, height});
                return;
            }

            ImGui::Dummy(ImVec2{ImGui::GetContentRegionAvail().x, std::min(120.0F, maxHeight)});
        }

        [[nodiscard]] std::string optionalResourceIndexText(std::optional<std::uint32_t> index) {
            return index ? std::to_string(*index) : std::string{"-"};
        }

        void
        drawSelectedExecutionEventDetails(const EditorI18n& i18n,
                                          const asharia::BasicRenderViewExecutionEvent& event) {
            textUnformatted(executionEventLabel(event));
            const std::string passText =
                std::string{i18n.text("renderGraph.pass")} + ": " + event.passName;
            ImGui::TextUnformatted(passText.c_str());
            if (event.commandIndex) {
                const std::string commandText = std::string{i18n.text("renderGraph.commands")} +
                                                ": " + std::to_string(*event.commandIndex);
                ImGui::TextUnformatted(commandText.c_str());
            }
            if (event.draw.vertexCount != 0 || event.draw.indexCount != 0) {
                const std::string drawText =
                    "Draw: vertices " + std::to_string(event.draw.vertexCount) + ", indices " +
                    std::to_string(event.draw.indexCount) + ", instances " +
                    std::to_string(event.draw.instanceCount);
                ImGui::TextUnformatted(drawText.c_str());
            }
            if (event.sourceImageResourceIndex || event.targetImageResourceIndex) {
                const std::string resourceText =
                    "Resources: source " +
                    optionalResourceIndexText(event.sourceImageResourceIndex) + ", target " +
                    optionalResourceIndexText(event.targetImageResourceIndex);
                ImGui::TextUnformatted(resourceText.c_str());
            }
        }

        void drawPassSummaryDetails(const EditorI18n& i18n,
                                    const asharia::RenderGraphDiagnosticsPassNode& pass) {
            textUnformatted(passEventLabel(pass));
            const std::string typeText = std::string{i18n.text("renderGraph.type")} + ": " +
                                         (pass.type.empty() ? "-" : pass.type);
            const std::string commandText = std::string{i18n.text("renderGraph.commands")} + ": " +
                                            std::to_string(pass.commandCount);
            const std::string transitionText =
                std::string{i18n.text("renderGraph.transitions")} + ": " +
                std::to_string(pass.imageTransitionCount + pass.bufferTransitionCount);
            ImGui::TextUnformatted(typeText.c_str());
            ImGui::TextUnformatted(commandText.c_str());
            ImGui::TextUnformatted(transitionText.c_str());
        }

        void drawPassCommandSummaryTable(const EditorI18n& i18n,
                                         const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                         const asharia::RenderGraphDiagnosticsPassNode& pass) {
            if (!ImGui::BeginTable("frame-debug-pass-commands", 3,
                                   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            const std::string commandIndexColumn{"#"};
            const std::string commandsColumn{i18n.text("renderGraph.commands")};
            const std::string detailColumn{i18n.text("frameDebug.detail")};
            ImGui::TableSetupColumn(commandIndexColumn.c_str());
            ImGui::TableSetupColumn(commandsColumn.c_str());
            ImGui::TableSetupColumn(detailColumn.c_str());
            ImGui::TableHeadersRow();
            for (const asharia::RenderGraphDiagnosticsCommandNode& command : snapshot.commands) {
                if (command.passIndex != pass.passIndex) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string commandIndexText = std::to_string(command.commandIndex);
                ImGui::TextUnformatted(commandIndexText.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(commandKindName(command.kind));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(command.detail.c_str());
            }
            ImGui::EndTable();
        }

        void drawPassAccessTable(const EditorI18n& i18n,
                                 const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                 const asharia::RenderGraphDiagnosticsPassNode& pass) {
            if (!ImGui::BeginTable("frame-debug-pass-access", 3,
                                   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            const std::string slotColumn{i18n.text("renderGraph.slot")};
            const std::string resourceColumn{i18n.text("renderGraph.resource")};
            const std::string useColumn{i18n.text("renderGraph.use")};
            ImGui::TableSetupColumn(slotColumn.c_str());
            ImGui::TableSetupColumn(resourceColumn.c_str());
            ImGui::TableSetupColumn(useColumn.c_str());
            ImGui::TableHeadersRow();
            for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
                if (edge.passIndex != pass.passIndex) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(edge.slotName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(edge.resourceName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(slotAccessName(edge.access));
            }
            ImGui::EndTable();
        }

        void drawSelectedPassDetails(const EditorI18n& i18n,
                                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                     const EditorFrameDebugPreview& preview) {
            const asharia::RenderGraphDiagnosticsPassNode* pass =
                selectedPassNode(snapshot, preview);
            if (pass == nullptr) {
                textUnformatted(i18n.text(EditorI18nTextQuery{
                    .key = "frameDebug.noPass",
                    .fallback = "No pass",
                }));
                return;
            }

            drawPassSummaryDetails(i18n, *pass);
            drawPassCommandSummaryTable(i18n, snapshot, *pass);
            drawPassAccessTable(i18n, snapshot, *pass);
        }

        void drawPassDetails(
            FrameDebuggerPanelContext& context,
            const asharia::BasicRenderViewDiagnostics& diagnostics,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            const asharia::BasicRenderViewExecutionEvent* event =
                selectedExecutionEvent(diagnostics, preview);
            if (event != nullptr) {
                drawSelectedExecutionEventDetails(context.ui->i18n, *event);
                ImGui::Separator();
            }

            drawSelectedPassDetails(context.ui->i18n, diagnostics.renderGraph, preview);
            ImGui::Separator();
            drawImageSelector(context, imageResources, preview);

            const bool previewVisible =
                preview.status == EditorFrameDebugPreviewStatus::Available &&
                hasEditorViewportTexture(preview.texture);
            context.frameDebugger->notifyFrameDebugPreviewDrawn(previewVisible);

            const std::string previewStatus =
                std::string{context.ui->i18n.text("frameDebug.preview")} + ": " +
                std::string{editorFrameDebugPreviewStatusName(preview.status)} +
                (preview.message.empty() ? std::string{} : " - " + preview.message);
            ImGui::TextUnformatted(previewStatus.c_str());
            drawPreviewTexture(preview, kPreviewImageMaxHeight);
        }

        [[nodiscard]] bool shouldSmokeSelectRenderGraphTab(FrameDebuggerPanelContext& context,
                                                           const EditorFrameDebugPreview& preview) {
            if (!context.ui->smokeMode ||
                preview.status != EditorFrameDebugPreviewStatus::Available ||
                !hasEditorViewportTexture(preview.texture)) {
                return false;
            }

            const EditorFrameDebuggerStats stats = context.frameDebugger->stats();
            return stats.previewTextureFramesDrawn > 0 &&
                   stats.frameDebugRenderGraphSnapshotFrames == 0;
        }

        void drawSnapshotPane(const EditorI18n& i18n, const EditorFrameDebugCapture& capture,
                              std::string_view status,
                              const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            drawRenderGraphSnapshotView(
                RenderGraphSnapshotViewDesc{
                    .sourceLabel = i18n.text("frameDebug.rgSource"),
                    .statusLabel = status,
                    .viewKind = capture.viewKind,
                    .requestedExtent = capture.requestedExtent,
                    .submittedFrameEpoch = capture.submittedFrameEpoch,
                    .i18n = i18n,
                },
                snapshot);
        }

        void drawFrameDebugMainView(
            FrameDebuggerPanelContext& context, const EditorFrameDebugCapture& capture,
            std::string_view status, const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            static_cast<void>(status);
            static_cast<void>(snapshot);

            const float availableWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
            float eventListWidth = std::clamp(availableWidth * kEventListWidthRatio,
                                              kEventListMinWidth, kEventListMaxWidth);
            if (availableWidth > kDetailsPaneMinWidth) {
                eventListWidth =
                    std::min(eventListWidth, std::max(kEventListNarrowWidth,
                                                      availableWidth - kDetailsPaneMinWidth));
            } else {
                eventListWidth = std::max(1.0F, availableWidth * 0.45F);
            }

            ImGui::BeginChild("frame-debug-event-list-pane", ImVec2{eventListWidth, 0.0F},
                              ImGuiChildFlags_Borders);
            textUnformatted(context.ui->i18n.text(EditorI18nTextQuery{
                .key = "frameDebug.events",
                .fallback = "Passes / Events",
            }));
            ImGui::Separator();
            drawPassEventList(context, capture.diagnostics, preview);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("frame-debug-details-pane", ImVec2{0.0F, 0.0F},
                              ImGuiChildFlags_Borders);
            drawPassDetails(context, capture.diagnostics, imageResources, preview);
            ImGui::EndChild();
        }

        void drawFrameDebugContent(
            FrameDebuggerPanelContext& context, const EditorFrameDebugCapture& capture,
            std::string_view status, const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            if (ImGui::BeginTabBar("frame-debugger-view-tabs")) {
                const std::string frameTab = context.ui->i18n.label(EditorI18nLabelDesc{
                    .key = "frameDebug.frameView",
                    .stableId = "frame-debug-frame-view",
                    .fallback = "Frame",
                });
                if (ImGui::BeginTabItem(frameTab.c_str())) {
                    drawFrameDebugMainView(context, capture, status, snapshot, imageResources,
                                           preview);
                    ImGui::EndTabItem();
                }

                const std::string graphTab = context.ui->i18n.label(EditorI18nLabelDesc{
                    .key = "frameDebug.graphView",
                    .stableId = "frame-debug-graph-view",
                    .fallback = "RenderGraph",
                });
                const ImGuiTabItemFlags graphTabFlags =
                    shouldSmokeSelectRenderGraphTab(context, preview)
                        ? ImGuiTabItemFlags_SetSelected
                        : ImGuiTabItemFlags_None;
                if (ImGui::BeginTabItem(graphTab.c_str(), nullptr, graphTabFlags)) {
                    context.frameDebugger->notifyFrameDebugRenderGraphViewDrawn(true);
                    drawSnapshotPane(context.ui->i18n, capture, status, snapshot);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
                return;
            }

            drawFrameDebugMainView(context, capture, status, snapshot, imageResources, preview);
        }

    } // namespace

    const EditorPanelDesc& FrameDebuggerPanel::desc() const {
        return desc_;
    }

    void FrameDebuggerPanel::prepareWindow(EditorPanelWindowContext& context,
                                           EditorPanelState& state) {
        static_cast<void>(state);
        if (context.ui.smokeMode) {
            ImGui::SetNextWindowSize(ImVec2{560.0F, 320.0F}, ImGuiCond_Always);
        }
    }

    void FrameDebuggerPanel::drawFrameDebuggerPanel(EditorFrameDebuggerPanelDrawContext& context,
                                                    EditorPanelState& state) {
        static_cast<void>(state);

        FrameDebuggerPanelContext panelContext{
            .ui = &context.ui,
            .frameDebugger = &context.frameDebugger,
        };
        const std::optional<EditorFrameDebugCapture>& pausedCapture =
            panelContext.frameDebugger->pausedCapture();
        const std::optional<EditorFrameDebugCapture>& latestCapture =
            pausedCapture ? pausedCapture : panelContext.frameDebugger->latestCapture();

        if (!latestCapture) {
            textUnformatted(panelContext.ui->i18n.text("frameDebug.noCapture"));
            return;
        }

        const std::string_view status =
            pausedCapture ? panelContext.ui->i18n.text("frameDebug.status.frozen")
                          : panelContext.ui->i18n.text("frameDebug.status.latestNotPaused");
        const asharia::RenderGraphDiagnosticsSnapshot& snapshot =
            latestCapture->diagnostics.renderGraph;
        const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*> imageResources =
            capturedImageResources(snapshot);
        const EditorFrameDebugPreview& preview = panelContext.frameDebugger->preview();
        drawFrameDebugContent(panelContext, *latestCapture, status, snapshot, imageResources,
                              preview);
    }

} // namespace asharia::editor

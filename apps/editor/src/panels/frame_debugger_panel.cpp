#include "panels/frame_debugger_panel.hpp"

#include <algorithm>
#include <imgui.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "editor_frame_debugger.hpp"
#include "panels/render_graph_snapshot_view.hpp"

namespace asharia::editor {
    namespace {

        constexpr float kPreviewPaneMinWidth = 240.0F;
        constexpr float kPreviewPaneMaxWidth = 360.0F;
        constexpr float kPreviewPaneNarrowWidth = 160.0F;
        constexpr float kPreviewPaneWidthRatio = 0.34F;
        constexpr float kRenderGraphPaneMinWidth = 260.0F;
        constexpr float kPreviewImageMaxHeight = 220.0F;

        [[nodiscard]] const char*
        imageFormatName(asharia::RenderGraphImageFormat format) {
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

        [[nodiscard]] std::string imageResourceLabel(
            const asharia::RenderGraphDiagnosticsResourceNode& resource) {
            return "#" + std::to_string(resource.resourceIndex) + " " + resource.name + " " +
                   imageFormatName(resource.imageFormat) + " " +
                   std::to_string(resource.imageExtent.width) + "x" +
                   std::to_string(resource.imageExtent.height);
        }

        [[nodiscard]] std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>
        capturedImageResources(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            std::vector<const asharia::RenderGraphDiagnosticsResourceNode*> images;
            for (const asharia::RenderGraphDiagnosticsResourceNode& resource :
                 snapshot.resources) {
                if (resource.kind == asharia::RenderGraphResourceKind::Image) {
                    images.push_back(&resource);
                }
            }
            return images;
        }

        void drawImageSelector(
            EditorFrameContext& context,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            if (imageResources.empty()) {
                ImGui::TextUnformatted("Image: -");
                return;
            }

            if (!preview.selectedImageResourceIndex) {
                static_cast<void>(
                    context.frameDebugger.selectPreviewImageResource(imageResources.front()->resourceIndex));
            }

            std::string selectedLabel = "No image";
            if (preview.selectedImageResourceIndex) {
                for (const asharia::RenderGraphDiagnosticsResourceNode* resource : imageResources) {
                    if (resource->resourceIndex == *preview.selectedImageResourceIndex) {
                        selectedLabel = imageResourceLabel(*resource);
                        break;
                    }
                }
            }

            ImGui::SetNextItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("Image", selectedLabel.c_str())) {
                for (const asharia::RenderGraphDiagnosticsResourceNode* resource :
                     imageResources) {
                    const bool selected = preview.selectedImageResourceIndex &&
                                          *preview.selectedImageResourceIndex ==
                                              resource->resourceIndex;
                    const std::string label = imageResourceLabel(*resource);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        static_cast<void>(
                            context.frameDebugger.selectPreviewImageResource(resource->resourceIndex));
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
                const float width = std::min(maxWidth, static_cast<float>(preview.texture.extent.width));
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

            ImGui::Dummy(ImVec2{ImGui::GetContentRegionAvail().x,
                                std::min(120.0F, maxHeight)});
        }

        void drawPreviewPane(
            EditorFrameContext& context,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            drawImageSelector(context, imageResources, preview);

            const bool previewVisible =
                preview.status == EditorFrameDebugPreviewStatus::Available &&
                hasEditorViewportTexture(preview.texture);
            context.frameDebugger.notifyFrameDebugPreviewDrawn(previewVisible);

            const std::string previewStatus =
                "Preview: " + std::string{editorFrameDebugPreviewStatusName(preview.status)} +
                (preview.message.empty() ? std::string{} : " - " + preview.message);
            ImGui::TextUnformatted(previewStatus.c_str());
            drawPreviewTexture(preview, kPreviewImageMaxHeight);
        }

        void drawSnapshotPane(const EditorFrameDebugCapture& capture, std::string_view status,
                              const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            drawRenderGraphSnapshotView(
                RenderGraphSnapshotViewDesc{
                    .sourceLabel = "Frame Debug RG View",
                    .statusLabel = status,
                    .viewKind = capture.viewKind,
                    .requestedExtent = capture.requestedExtent,
                    .submittedFrameEpoch = capture.submittedFrameEpoch,
                },
                snapshot);
        }

        void drawFrameDebugContent(
            EditorFrameContext& context, const EditorFrameDebugCapture& capture,
            std::string_view status,
            const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
            const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*>& imageResources,
            const EditorFrameDebugPreview& preview) {
            const float availableWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
            float previewWidth = std::clamp(availableWidth * kPreviewPaneWidthRatio,
                                            kPreviewPaneMinWidth, kPreviewPaneMaxWidth);
            if (availableWidth > kRenderGraphPaneMinWidth) {
                previewWidth =
                    std::min(previewWidth,
                             std::max(kPreviewPaneNarrowWidth,
                                      availableWidth - kRenderGraphPaneMinWidth));
            } else {
                previewWidth = std::max(1.0F, availableWidth * 0.45F);
            }

            ImGui::BeginChild("frame-debug-preview-pane", ImVec2{previewWidth, 0.0F},
                              ImGuiChildFlags_Borders);
            drawPreviewPane(context, imageResources, preview);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("frame-debug-rg-pane", ImVec2{0.0F, 0.0F},
                              ImGuiChildFlags_None);
            drawSnapshotPane(capture, status, snapshot);
            ImGui::EndChild();
        }

    } // namespace

    const EditorPanelDesc& FrameDebuggerPanel::desc() const {
        return desc_;
    }

    void FrameDebuggerPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);
        if (context.smokeMode) {
            ImGui::SetNextWindowSize(ImVec2{560.0F, 320.0F}, ImGuiCond_Always);
        }
    }

    void FrameDebuggerPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const std::optional<EditorFrameDebugCapture>& pausedCapture =
            context.frameDebugger.pausedCapture();
        const std::optional<EditorFrameDebugCapture>& latestCapture =
            pausedCapture ? pausedCapture : context.frameDebugger.latestCapture();

        context.frameDebugger.notifyFrameDebugRenderGraphViewDrawn(latestCapture.has_value());
        if (!latestCapture) {
            ImGui::TextUnformatted("No frame debug capture.");
            return;
        }

        const std::string status =
            pausedCapture ? "frozen captured frame" : "latest captured frame, not paused";
        const asharia::RenderGraphDiagnosticsSnapshot& snapshot =
            latestCapture->diagnostics.renderGraph;
        const std::vector<const asharia::RenderGraphDiagnosticsResourceNode*> imageResources =
            capturedImageResources(snapshot);
        const EditorFrameDebugPreview& preview = context.frameDebugger.preview();
        drawFrameDebugContent(context, *latestCapture, status, snapshot, imageResources, preview);
    }

} // namespace asharia::editor

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "editor_frame_debugger.hpp"
#include "editor_viewport_coordinator.hpp"

namespace {

    constexpr std::string_view kFrameDebugPreviewTextureOwnerId{"frame-debugger-preview"};

    VkExtent2D vkExtentFromEditor(asharia::editor::EditorExtent2D extent) {
        return VkExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    asharia::editor::EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return asharia::editor::EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    std::uint64_t nextSubmittedFrameEpoch(const asharia::VulkanFrameRecordContext& frame) {
        if (frame.frameLoop == nullptr) {
            return 0;
        }
        return frame.frameLoop->submittedFrameEpoch() + 1U;
    }

    const asharia::RenderGraphDiagnosticsResourceNode*
    findImageResource(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                      std::uint32_t resourceIndex) {
        for (const asharia::RenderGraphDiagnosticsResourceNode& resource : snapshot.resources) {
            if (resource.kind == asharia::RenderGraphResourceKind::Image &&
                resource.resourceIndex == resourceIndex) {
                return &resource;
            }
        }
        return nullptr;
    }

    VkFormat vkFormatFromRenderGraph(asharia::RenderGraphImageFormat format) {
        switch (format) {
        case asharia::RenderGraphImageFormat::B8G8R8A8Srgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case asharia::RenderGraphImageFormat::D32Sfloat:
            return VK_FORMAT_D32_SFLOAT;
        case asharia::RenderGraphImageFormat::Undefined:
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    asharia::BasicRenderViewWorldGridDesc
    replayWorldGridDesc(const asharia::BasicRenderViewOverlayDiagnostics& overlay) {
        return asharia::BasicRenderViewWorldGridDesc{
            .enabled = overlay.worldGridEnabled,
            .planeY = 0.0F,
            .minorSpacing = 1.0F,
            .majorSpacing = 10.0F,
            .fadeStart = 48.0F,
            .fadeEnd = 160.0F,
            .opacity = 1.0F,
        };
    }

    asharia::BasicRenderViewOverlayDesc
    replayOverlayDesc(const asharia::BasicRenderViewOverlayDiagnostics& overlay) {
        return asharia::BasicRenderViewOverlayDesc{
            .enabled = overlay.enabled,
            .colorLoadOp = overlay.colorLoadOp,
            .colorStoreOp = overlay.colorStoreOp,
            .blendMode = overlay.blendMode,
            .worldGrid = replayWorldGridDesc(overlay),
            .debugWorldLines = {},
        };
    }

} // namespace

namespace asharia::editor {

    asharia::Result<asharia::VulkanFrameRecordResult>
    EditorViewportCoordinator::recordFrameDebugPreview(
        const asharia::VulkanFrameRecordContext& frame,
        asharia::BasicFullscreenTextureRenderer& renderer, EditorFrameDebugger& frameDebugger) {
        const std::optional<EditorFrameDebugPreviewRequest> requestedPreview =
            frameDebugger.consumePreviewRequest();
        if (!requestedPreview) {
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const std::optional<EditorFrameDebugCapture>& capture = frameDebugger.pausedCapture();
        if (!capture) {
            frameDebugger.markPreviewUnavailable(requestedPreview->imageResourceIndex,
                                                 "No paused frame debug capture.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const asharia::RenderGraphDiagnosticsResourceNode* resource = findImageResource(
            capture->diagnostics.renderGraph, requestedPreview->imageResourceIndex);
        if (resource == nullptr) {
            frameDebugger.markPreviewUnavailable(requestedPreview->imageResourceIndex,
                                                 "Selected image resource was not captured.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }
        if (resource->imageFormat != asharia::RenderGraphImageFormat::B8G8R8A8Srgb) {
            frameDebugger.markPreviewUnavailable(requestedPreview->imageResourceIndex,
                                                 "Selected image format is not previewable.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const VkExtent2D replayExtent = vkExtentFromEditor(capture->requestedExtent);
        const VkExtent2D previewExtent{
            .width = resource->imageExtent.width,
            .height = resource->imageExtent.height,
        };
        const VkFormat previewFormat = vkFormatFromRenderGraph(resource->imageFormat);
        if (frame.format == VK_FORMAT_UNDEFINED || previewFormat == VK_FORMAT_UNDEFINED ||
            replayExtent.width == 0 || replayExtent.height == 0 || previewExtent.width == 0 ||
            previewExtent.height == 0) {
            frameDebugger.markPreviewUnavailable(requestedPreview->imageResourceIndex,
                                                 "Selected image preview shape is invalid.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        auto replayTarget = debugReplayTexture_.target.ensure(
            frame, asharia::VulkanRenderTargetDesc{
                       .device = device_,
                       .allocator = allocator_,
                       .format = frame.format,
                       .extent = replayExtent,
                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                   });
        if (!replayTarget) {
            return std::unexpected{std::move(replayTarget.error())};
        }
        auto previewTarget = debugPreviewTexture_.target.ensure(
            frame, asharia::VulkanRenderTargetDesc{
                       .device = device_,
                       .allocator = allocator_,
                       .format = previewFormat,
                       .extent = previewExtent,
                       .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                   });
        if (!previewTarget) {
            return std::unexpected{std::move(previewTarget.error())};
        }

        const asharia::VulkanSampledTextureView replayTexture =
            debugReplayTexture_.target.sampledTextureView();
        const asharia::VulkanSampledTextureView previewTexture =
            debugPreviewTexture_.target.sampledTextureView();
        asharia::BasicDebugPreviewResult previewResult;
        asharia::BasicDebugPreviewRequest previewRequest{
            .sourceImageResourceIndex = requestedPreview->imageResourceIndex,
            .afterPassIndex = requestedPreview->afterPassIndex,
            .target =
                asharia::BasicRenderViewTarget{
                    .image = previewTexture.image,
                    .imageView = previewTexture.imageView,
                    .format = previewTexture.format,
                    .extent = previewTexture.extent,
                    .aspectMask = previewTexture.aspectMask,
                    .finalUsage = asharia::BasicRenderViewTargetFinalUsage::SampledTexture,
                },
            .result = &previewResult,
        };

        asharia::BasicRenderViewDesc replayView;
        replayView.target = asharia::BasicRenderViewTarget{
            .image = replayTexture.image,
            .imageView = replayTexture.imageView,
            .format = replayTexture.format,
            .extent = replayTexture.extent,
            .aspectMask = replayTexture.aspectMask,
            .finalUsage = asharia::BasicRenderViewTargetFinalUsage::SampledTexture,
        };
        replayView.viewKind = capture->diagnostics.viewKind;
        replayView.camera = capture->diagnostics.camera;
        replayView.frameParams = capture->diagnostics.frameParams;
        replayView.overlay = replayOverlayDesc(capture->diagnostics.overlay);
        replayView.viewName = "Frame Debug Replay";
        replayView.debugPreview = &previewRequest;

        auto recorded = renderer.recordViewFrame(frame, replayView);
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        if (previewResult.status != asharia::BasicDebugPreviewStatus::Available ||
            previewResult.copiesRecorded == 0) {
            frameDebugger.markPreviewUnavailable(requestedPreview->imageResourceIndex,
                                                 previewResult.message.empty()
                                                     ? "Preview unavailable for selected image."
                                                     : previewResult.message);
            ++stats_.frameDebugPreviewUnavailableFrames;
            return *recorded;
        }

        auto published = textureRegistry_.registerOrUpdate(ImGuiTextureRegistration{
            .ownerId = kFrameDebugPreviewTextureOwnerId,
            .panelId = EditorId{.value = std::string{kFrameDebugPreviewTextureOwnerId}},
            .kind = EditorViewportKind::Preview,
            .requestedExtent = editorExtentFromVk(previewTexture.extent),
            .overlayFlags = {},
            .texture = previewTexture,
            .colorSpace = editorUiTextureColorSpaceFromVkFormat(previewTexture.format),
            .frameIndex = stats_.frameDebugPreviewFramesRecorded + 1U,
            .submittedFrameEpoch = nextSubmittedFrameEpoch(frame),
        });
        if (!published) {
            return std::unexpected{std::move(published.error())};
        }

        debugReplayTexture_.rendered = true;
        debugReplayTexture_.panelId = EditorId{.value = "frame-debugger-replay"};
        debugReplayTexture_.kind = EditorViewportKind::Preview;
        debugReplayTexture_.requestedExtent = capture->requestedExtent;
        debugReplayTexture_.format = replayTexture.format;
        debugReplayTexture_.extent = replayTexture.extent;
        debugPreviewTexture_.rendered = true;
        debugPreviewTexture_.panelId =
            EditorId{.value = std::string{kFrameDebugPreviewTextureOwnerId}};
        debugPreviewTexture_.kind = EditorViewportKind::Preview;
        debugPreviewTexture_.requestedExtent = editorExtentFromVk(previewTexture.extent);
        debugPreviewTexture_.format = previewTexture.format;
        debugPreviewTexture_.extent = previewTexture.extent;
        frameDebugger.publishPreviewTexture(requestedPreview->imageResourceIndex,
                                            published->texture, previewResult.copiedAfterPassIndex);
        ++stats_.frameDebugPreviewFramesRecorded;
        ++stats_.frameDebugPreviewTexturesPublished;
        return *recorded;
    }

} // namespace asharia::editor

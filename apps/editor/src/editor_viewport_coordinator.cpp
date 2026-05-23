#include "editor_viewport_coordinator.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_viewport_overlay_provider.hpp"

namespace {

    constexpr std::string_view kFrameDebugPreviewTextureOwnerId{"frame-debugger-preview"};

    bool sameExtent(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

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

    std::string_view editorViewportKindName(asharia::editor::EditorViewportKind kind) {
        switch (kind) {
        case asharia::editor::EditorViewportKind::Scene:
            return "Scene View";
        case asharia::editor::EditorViewportKind::Game:
            return "Game View";
        case asharia::editor::EditorViewportKind::Preview:
            return "Preview View";
        }
        return "Viewport";
    }

    asharia::BasicRenderViewKind basicRenderViewKind(asharia::editor::EditorViewportKind kind) {
        switch (kind) {
        case asharia::editor::EditorViewportKind::Scene:
            return asharia::BasicRenderViewKind::Scene;
        case asharia::editor::EditorViewportKind::Game:
            return asharia::BasicRenderViewKind::Game;
        case asharia::editor::EditorViewportKind::Preview:
            return asharia::BasicRenderViewKind::Preview;
        }
        return asharia::BasicRenderViewKind::Scene;
    }

    asharia::BasicRenderViewCamera
    basicRenderViewCamera(const asharia::editor::EditorViewportCamera& camera) {
        return asharia::BasicRenderViewCamera{
            .view = camera.view,
            .projection = camera.projection,
            .viewProjection = camera.viewProjection,
            .position = camera.position,
            .nearPlane = camera.nearPlane,
            .farPlane = camera.farPlane,
        };
    }

    asharia::BasicRenderViewFrameParams basicRenderViewFrameParams(std::uint64_t frameIndex) {
        return asharia::BasicRenderViewFrameParams{
            .frameIndex = frameIndex,
            .timeSeconds = 0.0F,
            .deltaSeconds = 0.0F,
            .renderScale = 1.0F,
        };
    }

    std::vector<asharia::BasicDebugWorldLine>
    basicDebugWorldLines(std::span<const asharia::editor::EditorViewportOverlayPacket> packets) {
        std::vector<asharia::BasicDebugWorldLine> lines;
        std::size_t lineCount = 0;
        for (const asharia::editor::EditorViewportOverlayPacket& packet : packets) {
            lineCount += packet.debugWorldLines.size();
        }
        lines.reserve(lineCount);
        for (const asharia::editor::EditorViewportOverlayPacket& packet : packets) {
            for (const asharia::editor::EditorDebugWorldLine& line : packet.debugWorldLines) {
                lines.push_back(asharia::BasicDebugWorldLine{
                    .start = line.start,
                    .end = line.end,
                    .color = line.color,
                });
            }
        }
        return lines;
    }

    asharia::BasicRenderViewOverlayDesc
    basicRenderViewOverlay(asharia::editor::EditorViewportOverlayFlags flags,
                           std::span<const asharia::BasicDebugWorldLine> debugWorldLines) {
        return asharia::BasicRenderViewOverlayDesc{
            .enabled = asharia::editor::anyEditorViewportOverlayFlagEnabled(flags),
            .colorLoadOp = asharia::BasicRenderViewOverlayColorLoadOp::LoadSceneColor,
            .colorStoreOp = asharia::BasicRenderViewOverlayColorStoreOp::Store,
            .blendMode = asharia::BasicRenderViewOverlayBlendMode::AlphaBlend,
            .debugWorldLines = debugWorldLines,
        };
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

    [[nodiscard]] std::uint64_t renderViewOverlayPassCount(
        const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        return static_cast<std::uint64_t>(std::ranges::count_if(
            snapshot.passes, [](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                return pass.type == asharia::kBasicRenderViewOverlayPassType;
            }));
    }

    [[nodiscard]] std::uint64_t renderViewOverlayCommandCount(
        const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        std::uint64_t commandCount = 0;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            if (pass.type == asharia::kBasicRenderViewOverlayPassType) {
                commandCount += static_cast<std::uint64_t>(pass.commandCount);
            }
        }
        return commandCount;
    }

} // namespace

namespace asharia::editor {

    EditorViewportFrameEpochs editorViewportFrameEpochs(const asharia::VulkanFrameLoop& frameLoop) {
        return EditorViewportFrameEpochs{
            .completedFrameEpoch = frameLoop.completedFrameEpoch(),
            .submittedFrameEpoch = frameLoop.submittedFrameEpoch() + 1U,
        };
    }

    bool EditorViewportCoordinator::ViewportTexture::ready() const {
        return rendered && format != VK_FORMAT_UNDEFINED && extent.width > 0 && extent.height > 0;
    }

    void EditorViewportCoordinator::ViewportTexture::clearMetadata() {
        format = VK_FORMAT_UNDEFINED;
        extent = {};
        panelId = {};
        kind = EditorViewportKind::Scene;
        requestedExtent = {};
        overlayFlags = {};
        diagnostics = {};
        frameIndex = {};
        rendered = false;
    }

    EditorViewportCoordinator::~EditorViewportCoordinator() {
        shutdown();
    }

    asharia::VoidResult EditorViewportCoordinator::create(const asharia::VulkanContext& context) {
        device_ = context.device();
        allocator_ = context.allocator();
        queue_ = context.graphicsQueue();
        return textureRegistry_.create(device_);
    }

    void EditorViewportCoordinator::beginImguiFrame(EditorViewportFrameEpochs epochs) {
        currentFrameSubmittedEpoch_ = epochs.submittedFrameEpoch;
        requestedViewport_.reset();
        textureRegistry_.beginFrame(epochs.completedFrameEpoch);
        promotePendingTexture();
    }

    void EditorViewportCoordinator::requestViewport(EditorViewportRequest request) {
        const EditorViewportOverlayFlags requestedFlags = request.overlayFlags;
        request.overlayFlags =
            effectiveEditorViewportOverlayFlags(request.kind, request.overlayFlags);
        if (request.kind != EditorViewportKind::Scene &&
            anyEditorSceneOnlyOverlayFlagEnabled(requestedFlags)) {
            ++stats_.sceneViewOnlyFlagRequestsDiscarded;
        }
        requestedViewport_ = std::move(request);
    }

    std::optional<EditorViewportResult>
    EditorViewportCoordinator::acquireViewportTextureForDraw(std::string_view panelId) {
        if (!presentedTexture_.ready() || presentedTexture_.panelId.value != panelId) {
            return std::nullopt;
        }

        auto result = textureRegistry_.acquireForDraw(panelId, currentFrameSubmittedEpoch_);
        if (!result) {
            return std::nullopt;
        }
        if (anyEditorViewportOverlayFlagEnabled(result->overlayFlags)) {
            ++stats_.overlayFlagTextureFramesAcquired;
        }
        ++textureFramesSubmitted_;
        return result;
    }

    asharia::Result<asharia::VulkanFrameRecordResult>
    EditorViewportCoordinator::recordRequestedViews(
        const asharia::VulkanFrameRecordContext& frame,
        asharia::BasicFullscreenTextureRenderer& renderer, bool recordRenderViews,
        EditorViewportRepaintReasons additionalRepaintReasons) {
        auto retired = processRetiredTextures(frame);
        if (!retired) {
            return std::unexpected{std::move(retired.error())};
        }

        if (!recordRenderViews || !requestedViewport_ ||
            !isRenderableEditorExtent(requestedViewport_->extent) ||
            frame.format == VK_FORMAT_UNDEFINED) {
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const EditorViewportRequest& request = *requestedViewport_;
        const VkExtent2D requestedExtent = vkExtentFromEditor(request.extent);
        const VkFormat requestedFormat = frame.format;
        EditorViewportRepaintReasons repaintReasons =
            request.refresh.repaintReasons | additionalRepaintReasons;
        const bool presentedTextureMatchesRequest =
            presentedTexture_.ready() && presentedTexture_.panelId.value == request.panelId.value &&
            presentedTexture_.kind == request.kind;
        if (!presentedTextureMatchesRequest) {
            addEditorViewportRepaintReason(repaintReasons,
                                           EditorViewportRepaintReason::InitialTextureMissing);
        } else {
            if (!sameExtent(presentedTexture_.extent, requestedExtent) ||
                presentedTexture_.format != requestedFormat) {
                addEditorViewportRepaintReason(repaintReasons, EditorViewportRepaintReason::Resize);
            }
            if (!sameEditorViewportOverlayFlags(presentedTexture_.overlayFlags,
                                                request.overlayFlags)) {
                addEditorViewportRepaintReason(repaintReasons,
                                               EditorViewportRepaintReason::OverlayFlagsChanged);
            }
        }
        if (request.refresh.policy == EditorViewportRefreshPolicy::Continuous) {
            addEditorViewportRepaintReason(repaintReasons,
                                           EditorViewportRepaintReason::AlwaysRefresh);
        }
        if (repaintReasons == 0U) {
            if (request.kind == EditorViewportKind::Scene) {
                ++stats_.idleSceneViewFramesSkipped;
            }
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const bool renderPresentedTexture =
            presentedTexture_.ready() && presentedTexture_.panelId.value == request.panelId.value &&
            presentedTexture_.kind == request.kind &&
            sameExtent(presentedTexture_.extent, requestedExtent) &&
            presentedTexture_.format == requestedFormat;
        ViewportTexture& renderTexture =
            renderPresentedTexture ? presentedTexture_ : pendingTexture_;

        auto ensured = renderTexture.target.ensure(
            frame, asharia::VulkanRenderTargetDesc{
                       .device = device_,
                       .allocator = allocator_,
                       .format = requestedFormat,
                       .extent = requestedExtent,
                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                   });
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }

        const asharia::VulkanSampledTextureView texture = renderTexture.target.sampledTextureView();
        asharia::BasicRenderViewDiagnostics diagnostics;
        const std::uint64_t viewportFrameIndex = viewportFramesRendered_ + 1U;
        const EditorViewportOverlayPacketList overlayPackets =
            collectBuiltInEditorViewportOverlayPackets(EditorViewportOverlayProviderContext{
                .viewportKind = request.kind,
                .camera = request.camera,
                .overlayFlags = request.overlayFlags,
            });
        const std::vector<asharia::BasicDebugWorldLine> debugWorldLines =
            basicDebugWorldLines(overlayPackets.packets);

        auto recorded = renderer.recordViewFrame(
            frame,
            asharia::BasicRenderViewDesc{
                .target =
                    asharia::BasicRenderViewTarget{
                        .image = texture.image,
                        .imageView = texture.imageView,
                        .format = texture.format,
                        .extent = texture.extent,
                        .aspectMask = texture.aspectMask,
                        .finalUsage = asharia::BasicRenderViewTargetFinalUsage::SampledTexture,
                    },
                .viewKind = basicRenderViewKind(request.kind),
                .camera = basicRenderViewCamera(request.camera),
                .frameParams = basicRenderViewFrameParams(viewportFrameIndex),
                .overlay = basicRenderViewOverlay(request.overlayFlags, debugWorldLines),
                .viewName = editorViewportKindName(request.kind),
                .diagnostics = &diagnostics,
            });
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        auto published = textureRegistry_.registerOrUpdate(ImGuiTextureRegistration{
            .ownerId = request.panelId.value,
            .kind = request.kind,
            .requestedExtent = request.extent,
            .overlayFlags = request.overlayFlags,
            .texture = texture,
            .colorSpace = editorUiTextureColorSpaceFromVkFormat(texture.format),
            .frameIndex = viewportFrameIndex,
            .submittedFrameEpoch = nextSubmittedFrameEpoch(frame),
        });
        if (!published) {
            return std::unexpected{std::move(published.error())};
        }

        renderTexture.rendered = true;
        renderTexture.panelId = request.panelId;
        renderTexture.kind = request.kind;
        renderTexture.requestedExtent = request.extent;
        renderTexture.overlayFlags = request.overlayFlags;
        renderTexture.diagnostics = std::move(diagnostics);
        renderTexture.frameIndex = viewportFrameIndex;
        renderTexture.format = texture.format;
        renderTexture.extent = texture.extent;
        latestRecordedDiagnostics_ = EditorRecordedRenderViewDiagnostics{
            .kind = request.kind,
            .requestedExtent = request.extent,
            .submittedFrameEpoch = nextSubmittedFrameEpoch(frame),
            .diagnostics = renderTexture.diagnostics,
        };
        ++stats_.renderViewDiagnosticsFramesRecorded;
        ++stats_.repaintReasonFramesRecorded;
        stats_.lastRenderViewDiagnosticsPasses =
            renderTexture.diagnostics.renderGraph.passes.size();
        stats_.lastRenderViewDiagnosticsResources =
            renderTexture.diagnostics.renderGraph.resources.size();
        stats_.lastRenderViewDiagnosticsAccessEdges =
            renderTexture.diagnostics.renderGraph.accessEdges.size();
        stats_.lastRenderViewDiagnosticsDependencyEdges =
            renderTexture.diagnostics.renderGraph.dependencyEdges.size();
        stats_.lastRenderViewDiagnosticsTransitions =
            renderTexture.diagnostics.renderGraph.transitions.size();
        stats_.lastRenderViewDiagnosticsExecutionEvents =
            renderTexture.diagnostics.executionEvents.size();
        stats_.lastRenderViewDiagnosticsOverlayPasses =
            renderViewOverlayPassCount(renderTexture.diagnostics.renderGraph);
        stats_.lastRenderViewDiagnosticsOverlayCommands =
            renderViewOverlayCommandCount(renderTexture.diagnostics.renderGraph);
        stats_.lastRenderViewDiagnosticsKind = renderTexture.diagnostics.viewKind;
        stats_.lastRenderViewDiagnosticsFrameIndex =
            renderTexture.diagnostics.frameParams.frameIndex;
        stats_.lastRenderViewDiagnosticsOverlayEnabled = renderTexture.diagnostics.overlay.enabled;
        stats_.lastRenderViewDiagnosticsDebugWorldLines =
            renderTexture.diagnostics.overlay.debugWorldLineCount;
        stats_.lastRenderViewDiagnosticsCameraPosition = renderTexture.diagnostics.camera.position;
        stats_.lastRenderViewDiagnosticsCameraNearPlane =
            renderTexture.diagnostics.camera.nearPlane;
        stats_.lastRenderViewDiagnosticsCameraFarPlane = renderTexture.diagnostics.camera.farPlane;
        stats_.lastRenderViewDiagnosticsCameraProjectionXScale =
            renderTexture.diagnostics.camera.projection[0];
        stats_.lastRenderViewDiagnosticsCameraProjectionYScale =
            renderTexture.diagnostics.camera.projection[5];
        stats_.lastRenderViewDiagnosticsCameraViewProjectionDepthScale =
            renderTexture.diagnostics.camera.viewProjection[10];
        if (anyEditorViewportOverlayFlagEnabled(request.overlayFlags)) {
            ++stats_.overlayFlagFramesRendered;
        }
        ++viewportFramesRendered_;
        return *recorded;
    }

    asharia::Result<asharia::VulkanFrameRecordResult>
    EditorViewportCoordinator::recordFrameDebugPreview(
        const asharia::VulkanFrameRecordContext& frame,
        asharia::BasicFullscreenTextureRenderer& renderer, EditorFrameDebugger& frameDebugger) {
        const std::optional<std::uint32_t> requestedImage = frameDebugger.consumePreviewRequest();
        if (!requestedImage) {
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const std::optional<EditorFrameDebugCapture>& capture = frameDebugger.pausedCapture();
        if (!capture) {
            frameDebugger.markPreviewUnavailable(*requestedImage, "No paused frame debug capture.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const asharia::RenderGraphDiagnosticsResourceNode* resource =
            findImageResource(capture->diagnostics.renderGraph, *requestedImage);
        if (resource == nullptr) {
            frameDebugger.markPreviewUnavailable(*requestedImage,
                                                 "Selected image resource was not captured.");
            ++stats_.frameDebugPreviewUnavailableFrames;
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }
        if (resource->imageFormat != asharia::RenderGraphImageFormat::B8G8R8A8Srgb) {
            frameDebugger.markPreviewUnavailable(*requestedImage,
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
            frameDebugger.markPreviewUnavailable(*requestedImage,
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
            .sourceImageResourceIndex = *requestedImage,
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
        replayView.viewKind = asharia::BasicRenderViewKind::Preview;
        replayView.viewName = "Frame Debug Replay";
        replayView.debugPreview = &previewRequest;

        auto recorded = renderer.recordViewFrame(frame, replayView);
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        if (previewResult.status != asharia::BasicDebugPreviewStatus::Available ||
            previewResult.copiesRecorded == 0) {
            frameDebugger.markPreviewUnavailable(*requestedImage,
                                                 previewResult.message.empty()
                                                     ? "Preview unavailable for selected image."
                                                     : previewResult.message);
            ++stats_.frameDebugPreviewUnavailableFrames;
            return *recorded;
        }

        auto published = textureRegistry_.registerOrUpdate(ImGuiTextureRegistration{
            .ownerId = kFrameDebugPreviewTextureOwnerId,
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
        frameDebugger.publishPreviewTexture(*requestedImage, published->texture);
        ++stats_.frameDebugPreviewFramesRecorded;
        ++stats_.frameDebugPreviewTexturesPublished;
        return *recorded;
    }

    void EditorViewportCoordinator::shutdown() {
        if (!hasTextureToRelease()) {
            textureRegistry_.shutdown();
            return;
        }

        if (queue_ != VK_NULL_HANDLE) {
            const VkResult idleResult = vkQueueWaitIdle(queue_);
            if (idleResult != VK_SUCCESS) {
                asharia::logError("Failed to wait for Vulkan queue before editor viewport "
                                  "texture shutdown: " +
                                  asharia::vkResultName(idleResult));
            }
        }
        textureRegistry_.shutdown();
        presentedTexture_ = {};
        pendingTexture_ = {};
        debugReplayTexture_ = {};
        debugPreviewTexture_ = {};
        retiredTextures_.clear();
    }

    bool EditorViewportCoordinator::hasPresentedViewportTexture() const {
        return viewportFramesRendered_ > 0 && textureFramesSubmitted_ > 0;
    }

    VkExtent2D EditorViewportCoordinator::descriptorExtent() const {
        if (presentedTexture_.ready()) {
            return presentedTexture_.extent;
        }
        return pendingTexture_.extent;
    }

    std::uint64_t EditorViewportCoordinator::viewportFramesRendered() const {
        return viewportFramesRendered_;
    }

    std::uint64_t EditorViewportCoordinator::textureFramesSubmitted() const {
        return textureFramesSubmitted_;
    }

    EditorViewportCoordinatorStats EditorViewportCoordinator::stats() const {
        return stats_;
    }

    ImGuiTextureRegistryStats EditorViewportCoordinator::textureRegistryStats() const {
        return textureRegistry_.stats();
    }

    const std::optional<EditorRecordedRenderViewDiagnostics>&
    EditorViewportCoordinator::latestRecordedRenderViewDiagnostics() const {
        return latestRecordedDiagnostics_;
    }

    std::optional<EditorRenderGraphSnapshot>
    EditorViewportCoordinator::latestLiveRenderGraphSnapshot() const {
        if (!latestRecordedDiagnostics_) {
            return std::nullopt;
        }

        return EditorRenderGraphSnapshot{
            .viewKind = latestRecordedDiagnostics_->kind,
            .requestedExtent = latestRecordedDiagnostics_->requestedExtent,
            .submittedFrameEpoch = latestRecordedDiagnostics_->submittedFrameEpoch,
            .snapshot = &latestRecordedDiagnostics_->diagnostics.renderGraph,
        };
    }

    void EditorViewportCoordinator::notifyLiveRenderGraphViewDrawn(bool snapshotVisible) {
        ++stats_.liveRenderGraphViewFrames;
        if (snapshotVisible) {
            ++stats_.liveRenderGraphSnapshotFrames;
        }
    }

    void EditorViewportCoordinator::promotePendingTexture() {
        if (!pendingTexture_.ready()) {
            return;
        }
        if (presentedTexture_.ready()) {
            retiredTextures_.push_back(std::move(presentedTexture_));
            ++stats_.renderTargetsRetired;
        }
        presentedTexture_ = std::move(pendingTexture_);
        pendingTexture_ = {};
    }

    bool EditorViewportCoordinator::hasTextureToRelease() const {
        return presentedTexture_.ready() || pendingTexture_.ready() || !textureRegistry_.empty() ||
               debugReplayTexture_.ready() || debugPreviewTexture_.ready() ||
               std::ranges::any_of(retiredTextures_,
                                   [](const ViewportTexture& texture) { return texture.ready(); });
    }

    asharia::VoidResult EditorViewportCoordinator::processRetiredTextures(
        const asharia::VulkanFrameRecordContext& frame) {
        for (ViewportTexture& texture : retiredTextures_) {
            if (!texture.target.deferDestroy(frame)) {
                return std::unexpected{
                    asharia::vulkanError("Failed to defer retired editor viewport texture")};
            }
            ++stats_.renderTargetsDeferred;
            texture.clearMetadata();
        }
        retiredTextures_.clear();
        return {};
    }

} // namespace asharia::editor

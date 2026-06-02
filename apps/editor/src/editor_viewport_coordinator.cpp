#include "editor_viewport_coordinator.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_viewport_overlay_provider.hpp"

namespace {

    bool sameExtent(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    VkExtent2D vkExtentFromEditor(asharia::editor::EditorExtent2D extent) {
        return VkExtent2D{
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

    std::string_view editorViewportKindKey(asharia::editor::EditorViewportKind kind) {
        switch (kind) {
        case asharia::editor::EditorViewportKind::Scene:
            return "scene";
        case asharia::editor::EditorViewportKind::Game:
            return "game";
        case asharia::editor::EditorViewportKind::Preview:
            return "preview";
        }
        return "viewport";
    }

    std::string viewportTextureOwnerId(std::string_view panelId,
                                       asharia::editor::EditorViewportKind kind) {
        std::string ownerId;
        ownerId.reserve(panelId.size() + 10U);
        ownerId.append(panelId);
        ownerId.append("#");
        ownerId.append(editorViewportKindKey(kind));
        return ownerId;
    }

    asharia::Error withViewportContext(asharia::Error error,
                                       const asharia::editor::EditorViewportRequest& request,
                                       std::string_view operation) {
        error.message = std::string{operation} + " for " +
                        std::string{editorViewportKindName(request.kind)} + " '" +
                        request.panelId.value + "': " + error.message;
        return error;
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

    void appendUniqueSourceOverlayId(std::vector<std::string_view>& ids,
                                     std::string_view overlayId) {
        if (overlayId.empty() || std::ranges::find(ids, overlayId) != ids.end()) {
            return;
        }
        ids.push_back(overlayId);
    }

    std::vector<std::string_view> basicRenderViewSourceOverlayIds(
        asharia::editor::EditorViewportOverlayFlags flags,
        std::span<const asharia::editor::EditorViewportOverlayPacket> packets) {
        std::vector<std::string_view> ids;
        ids.reserve(5U + packets.size());
        if (flags.gridVisible) {
            appendUniqueSourceOverlayId(ids, asharia::editor::kEditorSceneGridOverlayId);
        }
        if (flags.gizmoVisible) {
            appendUniqueSourceOverlayId(ids,
                                        asharia::editor::kEditorSceneTransformGizmoOverlayId);
        }
        if (flags.selectionOutlineVisible) {
            appendUniqueSourceOverlayId(ids,
                                        asharia::editor::kEditorSceneSelectionOutlineOverlayId);
        }
        if (flags.debugOverlayVisible) {
            appendUniqueSourceOverlayId(ids, asharia::editor::kEditorDebugOverlayId);
        }
        if (flags.debugGizmoVisible) {
            appendUniqueSourceOverlayId(ids, asharia::editor::kEditorDebugGizmoOverlayId);
        }
        for (const asharia::editor::EditorViewportOverlayPacket& packet : packets) {
            appendUniqueSourceOverlayId(ids, packet.overlayId);
        }
        return ids;
    }

    asharia::BasicRenderViewOverlayDesc
    basicRenderViewOverlay(asharia::editor::EditorViewportOverlayFlags flags,
                           asharia::editor::EditorViewportWorldGridSettings worldGrid,
                           std::span<const asharia::BasicDebugWorldLine> debugWorldLines,
                           std::span<const std::string_view> sourceOverlayIds) {
        return asharia::BasicRenderViewOverlayDesc{
            .enabled = asharia::editor::anyEditorViewportOverlayFlagEnabled(flags),
            .colorLoadOp = asharia::BasicRenderViewOverlayColorLoadOp::LoadSceneColor,
            .colorStoreOp = asharia::BasicRenderViewOverlayColorStoreOp::Store,
            .blendMode = asharia::BasicRenderViewOverlayBlendMode::AlphaBlend,
            .worldGrid =
                asharia::BasicRenderViewWorldGridDesc{
                    .enabled = flags.gridVisible,
                    .planeY = worldGrid.planeY,
                    .minorSpacing = worldGrid.minorSpacing,
                    .majorSpacing = worldGrid.majorSpacing,
                    .fadeStart = worldGrid.fadeStart,
                    .fadeEnd = worldGrid.fadeEnd,
                    .opacity = worldGrid.opacity,
                    .color = worldGrid.color,
                },
            .sourceOverlayIds = sourceOverlayIds,
            .debugWorldLines = debugWorldLines,
        };
    }

    [[nodiscard]] std::uint64_t
    renderViewOverlayPassCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        return static_cast<std::uint64_t>(std::ranges::count_if(
            snapshot.passes, [](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                return pass.type == asharia::kBasicRenderViewOverlayPassType;
            }));
    }

    [[nodiscard]] std::uint64_t
    renderViewOverlayCommandCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
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
        worldGrid = {};
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
        for (ViewportSlot& slot : viewportSlots_) {
            slot.requestedViewport.reset();
        }
        textureRegistry_.beginFrame(epochs.completedFrameEpoch);
        promotePendingTextures();
    }

    void EditorViewportCoordinator::requestViewport(EditorViewportRequest request) {
        const EditorViewportOverlayFlags requestedFlags = request.overlayFlags;
        request.overlayFlags =
            effectiveEditorViewportOverlayFlags(request.kind, request.overlayFlags);
        if (request.kind != EditorViewportKind::Scene &&
            anyEditorSceneOnlyOverlayFlagEnabled(requestedFlags)) {
            ++stats_.sceneViewOnlyFlagRequestsDiscarded;
        }
        ViewportSlot& slot = findOrCreateViewportSlot(request);
        slot.requestedViewport = std::move(request);
        ++stats_.viewportRequestsQueued;
    }

    std::optional<EditorViewportResult>
    EditorViewportCoordinator::acquireViewportTextureForDraw(std::string_view panelId,
                                                             EditorViewportKind kind) {
        const ViewportSlot* slot = findPresentedViewportSlotForView(panelId, kind);
        if (slot == nullptr) {
            return std::nullopt;
        }

        const std::string ownerId = viewportTextureOwnerId(slot->panelId.value, slot->kind);
        auto result = textureRegistry_.acquireForDraw(ownerId, currentFrameSubmittedEpoch_);
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
        stats_.lastFrameViewportRequests = 0;
        stats_.lastFrameRenderViewsRecorded = 0;
        stats_.lastFrameRenderViewsSkipped = 0;

        auto retired = processRetiredTextures(frame);
        if (!retired) {
            return std::unexpected{std::move(retired.error())};
        }

        if (!recordRenderViews || frame.format == VK_FORMAT_UNDEFINED) {
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        asharia::VulkanFrameRecordResult combined{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };

        for (ViewportSlot& slot : viewportSlots_) {
            if (!slot.requestedViewport ||
                !isRenderableEditorExtent(slot.requestedViewport->extent)) {
                continue;
            }

            const EditorViewportRequest& request = *slot.requestedViewport;
            ++stats_.lastFrameViewportRequests;
            const VkExtent2D requestedExtent = vkExtentFromEditor(request.extent);
            const VkFormat requestedFormat = frame.format;
            const EditorViewportRepaintReasons repaintReasons = repaintReasonsForViewportRequest(
                slot, request, requestedExtent, requestedFormat, additionalRepaintReasons);
            if (repaintReasons == 0U) {
                if (request.kind == EditorViewportKind::Scene) {
                    ++stats_.idleSceneViewFramesSkipped;
                }
                ++stats_.lastFrameRenderViewsSkipped;
                continue;
            }

            auto recorded = recordRequestedViewportSlot(frame, renderer, slot, request,
                                                        requestedExtent, requestedFormat);
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }
            combined.waitStageMask |= recorded->waitStageMask;
        }
        if (stats_.lastFrameRenderViewsRecorded > 1U) {
            ++stats_.multiViewFramesRecorded;
        }
        return combined;
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
        viewportSlots_.clear();
        debugReplayTexture_ = {};
        debugPreviewTexture_ = {};
        retiredTextures_.clear();
    }

    bool EditorViewportCoordinator::hasPresentedViewportTexture() const {
        return viewportFramesRendered_ > 0 && textureFramesSubmitted_ > 0;
    }

    VkExtent2D EditorViewportCoordinator::descriptorExtent() const {
        for (const ViewportSlot& slot : viewportSlots_) {
            if (slot.presentedTexture.ready()) {
                return slot.presentedTexture.extent;
            }
        }
        for (const ViewportSlot& slot : viewportSlots_) {
            if (slot.pendingTexture.ready()) {
                return slot.pendingTexture.extent;
            }
        }
        return {};
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

    std::optional<EditorRecordedRenderViewDiagnostics>
    EditorViewportCoordinator::latestRecordedRenderViewDiagnosticsForView(
        std::string_view panelId, EditorViewportKind kind) const {
        const ViewportSlot* slot = findViewportSlot(panelId, kind);
        if (slot == nullptr) {
            return std::nullopt;
        }
        return slot->latestRecordedDiagnostics;
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

    EditorViewportCoordinator::ViewportSlot*
    EditorViewportCoordinator::findViewportSlot(std::string_view panelId, EditorViewportKind kind) {
        auto slot = std::ranges::find_if(viewportSlots_, [panelId, kind](const ViewportSlot& item) {
            return item.panelId.value == panelId && item.kind == kind;
        });
        return slot == viewportSlots_.end() ? nullptr : &*slot;
    }

    const EditorViewportCoordinator::ViewportSlot*
    EditorViewportCoordinator::findViewportSlot(std::string_view panelId,
                                                EditorViewportKind kind) const {
        auto slot = std::ranges::find_if(viewportSlots_, [panelId, kind](const ViewportSlot& item) {
            return item.panelId.value == panelId && item.kind == kind;
        });
        return slot == viewportSlots_.end() ? nullptr : &*slot;
    }

    EditorViewportCoordinator::ViewportSlot&
    EditorViewportCoordinator::findOrCreateViewportSlot(const EditorViewportRequest& request) {
        if (ViewportSlot* slot = findViewportSlot(request.panelId.value, request.kind);
            slot != nullptr) {
            return *slot;
        }
        ViewportSlot& slot = viewportSlots_.emplace_back();
        slot.panelId = request.panelId;
        slot.kind = request.kind;
        return slot;
    }

    const EditorViewportCoordinator::ViewportSlot*
    EditorViewportCoordinator::findPresentedViewportSlotForView(std::string_view panelId,
                                                                EditorViewportKind kind) const {
        auto slot = std::ranges::find_if(viewportSlots_, [panelId, kind](const ViewportSlot& item) {
            return item.panelId.value == panelId && item.kind == kind &&
                   item.presentedTexture.ready();
        });
        return slot == viewportSlots_.end() ? nullptr : &*slot;
    }

    void EditorViewportCoordinator::promotePendingTextures() {
        for (ViewportSlot& slot : viewportSlots_) {
            if (!slot.pendingTexture.ready()) {
                continue;
            }
            if (slot.presentedTexture.ready()) {
                retiredTextures_.push_back(std::move(slot.presentedTexture));
                ++stats_.renderTargetsRetired;
            }
            slot.presentedTexture = std::move(slot.pendingTexture);
            slot.pendingTexture = {};
        }
    }

    bool EditorViewportCoordinator::hasTextureToRelease() const {
        const bool hasViewportSlotTexture =
            std::ranges::any_of(viewportSlots_, [](const ViewportSlot& slot) {
                return slot.presentedTexture.ready() || slot.pendingTexture.ready();
            });
        return hasViewportSlotTexture || !textureRegistry_.empty() || debugReplayTexture_.ready() ||
               debugPreviewTexture_.ready() ||
               std::ranges::any_of(retiredTextures_,
                                   [](const ViewportTexture& texture) { return texture.ready(); });
    }

    EditorViewportRepaintReasons EditorViewportCoordinator::repaintReasonsForViewportRequest(
        const ViewportSlot& slot, const EditorViewportRequest& request, VkExtent2D requestedExtent,
        VkFormat requestedFormat, EditorViewportRepaintReasons additionalRepaintReasons) {
        EditorViewportRepaintReasons repaintReasons =
            request.refresh.repaintReasons | additionalRepaintReasons;
        const bool presentedTextureMatchesRequest =
            slot.presentedTexture.ready() &&
            slot.presentedTexture.panelId.value == request.panelId.value &&
            slot.presentedTexture.kind == request.kind;
        if (!presentedTextureMatchesRequest) {
            addEditorViewportRepaintReason(repaintReasons,
                                           EditorViewportRepaintReason::InitialTextureMissing);
        } else {
            if (!sameExtent(slot.presentedTexture.extent, requestedExtent) ||
                slot.presentedTexture.format != requestedFormat) {
                addEditorViewportRepaintReason(repaintReasons, EditorViewportRepaintReason::Resize);
            }
            if (!sameEditorViewportOverlayFlags(slot.presentedTexture.overlayFlags,
                                                request.overlayFlags)) {
                addEditorViewportRepaintReason(repaintReasons,
                                               EditorViewportRepaintReason::OverlayFlagsChanged);
            }
            const bool comparesWorldGrid =
                slot.presentedTexture.overlayFlags.gridVisible || request.overlayFlags.gridVisible;
            if (comparesWorldGrid && !sameEditorViewportWorldGridSettings(
                                         slot.presentedTexture.worldGrid, request.worldGrid)) {
                addEditorViewportRepaintReason(repaintReasons,
                                               EditorViewportRepaintReason::OverlayFlagsChanged);
            }
        }
        if (request.refresh.policy == EditorViewportRefreshPolicy::Continuous) {
            addEditorViewportRepaintReason(repaintReasons,
                                           EditorViewportRepaintReason::AlwaysRefresh);
        }
        return repaintReasons;
    }

    asharia::Result<asharia::VulkanFrameRecordResult>
    EditorViewportCoordinator::recordRequestedViewportSlot(
        const asharia::VulkanFrameRecordContext& frame,
        asharia::BasicFullscreenTextureRenderer& renderer, ViewportSlot& slot,
        const EditorViewportRequest& request, VkExtent2D requestedExtent,
        VkFormat requestedFormat) {
        const bool renderPresentedTexture =
            slot.presentedTexture.ready() &&
            slot.presentedTexture.panelId.value == request.panelId.value &&
            slot.presentedTexture.kind == request.kind &&
            sameExtent(slot.presentedTexture.extent, requestedExtent) &&
            slot.presentedTexture.format == requestedFormat;
        ViewportTexture& renderTexture =
            renderPresentedTexture ? slot.presentedTexture : slot.pendingTexture;

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
            return std::unexpected{
                withViewportContext(std::move(ensured.error()), request,
                                    "Failed to ensure editor viewport render target")};
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
        const std::vector<std::string_view> sourceOverlayIds =
            basicRenderViewSourceOverlayIds(request.overlayFlags, overlayPackets.packets);

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
                .overlay = basicRenderViewOverlay(request.overlayFlags, request.worldGrid,
                                                  debugWorldLines, sourceOverlayIds),
                .viewName = editorViewportKindName(request.kind),
                .diagnostics = &diagnostics,
            });
        if (!recorded) {
            return std::unexpected{withViewportContext(std::move(recorded.error()), request,
                                                       "Failed to record editor RenderView")};
        }

        const std::uint64_t submittedFrameEpoch = nextSubmittedFrameEpoch(frame);
        const std::string ownerId = viewportTextureOwnerId(request.panelId.value, request.kind);
        auto published = textureRegistry_.registerOrUpdate(ImGuiTextureRegistration{
            .ownerId = ownerId,
            .panelId = request.panelId,
            .kind = request.kind,
            .requestedExtent = request.extent,
            .overlayFlags = request.overlayFlags,
            .texture = texture,
            .colorSpace = editorUiTextureColorSpaceFromVkFormat(texture.format),
            .frameIndex = viewportFrameIndex,
            .submittedFrameEpoch = submittedFrameEpoch,
        });
        if (!published) {
            return std::unexpected{
                withViewportContext(std::move(published.error()), request,
                                    "Failed to publish editor viewport texture")};
        }

        renderTexture.rendered = true;
        renderTexture.panelId = request.panelId;
        renderTexture.kind = request.kind;
        renderTexture.requestedExtent = request.extent;
        renderTexture.overlayFlags = request.overlayFlags;
        renderTexture.worldGrid = request.worldGrid;
        renderTexture.diagnostics = std::move(diagnostics);
        renderTexture.frameIndex = viewportFrameIndex;
        renderTexture.format = texture.format;
        renderTexture.extent = texture.extent;
        slot.latestRecordedDiagnostics = EditorRecordedRenderViewDiagnostics{
            .panelId = request.panelId,
            .kind = request.kind,
            .requestedExtent = request.extent,
            .submittedFrameEpoch = submittedFrameEpoch,
            .diagnostics = renderTexture.diagnostics,
        };
        latestRecordedDiagnostics_ = slot.latestRecordedDiagnostics;
        updateRenderViewDiagnosticStats(renderTexture);
        if (anyEditorViewportOverlayFlagEnabled(request.overlayFlags)) {
            ++stats_.overlayFlagFramesRendered;
        }
        ++stats_.viewportRequestsRecorded;
        ++stats_.lastFrameRenderViewsRecorded;
        ++viewportFramesRendered_;
        return *recorded;
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

    void EditorViewportCoordinator::updateRenderViewDiagnosticStats(
        const ViewportTexture& renderTexture) {
        ++stats_.renderViewDiagnosticsFramesRecorded;
        switch (renderTexture.kind) {
        case EditorViewportKind::Scene:
            ++stats_.sceneViewDiagnosticsFramesRecorded;
            break;
        case EditorViewportKind::Game:
            ++stats_.gameViewDiagnosticsFramesRecorded;
            break;
        case EditorViewportKind::Preview:
            ++stats_.previewViewDiagnosticsFramesRecorded;
            break;
        }
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
    }

} // namespace asharia::editor

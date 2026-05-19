#include "editor_viewport_coordinator.hpp"

#include <algorithm>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

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
        ++textureFramesSubmitted_;
        return result;
    }

    asharia::Result<asharia::VulkanFrameRecordResult>
    EditorViewportCoordinator::recordRequestedViews(
        const asharia::VulkanFrameRecordContext& frame,
        asharia::BasicFullscreenTextureRenderer& renderer) {
        auto retired = processRetiredTextures(frame);
        if (!retired) {
            return std::unexpected{std::move(retired.error())};
        }

        if (!requestedViewport_ || !isRenderableEditorExtent(requestedViewport_->extent) ||
            frame.format == VK_FORMAT_UNDEFINED) {
            return asharia::VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
        }

        const EditorViewportRequest& request = *requestedViewport_;
        const VkExtent2D requestedExtent = vkExtentFromEditor(request.extent);
        const VkFormat requestedFormat = frame.format;
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
            });
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        auto published = textureRegistry_.registerOrUpdate(ImGuiTextureRegistration{
            .ownerId = request.panelId.value,
            .kind = request.kind,
            .requestedExtent = request.extent,
            .texture = texture,
            .submittedFrameEpoch = nextSubmittedFrameEpoch(frame),
        });
        if (!published) {
            return std::unexpected{std::move(published.error())};
        }

        renderTexture.rendered = true;
        renderTexture.panelId = request.panelId;
        renderTexture.kind = request.kind;
        renderTexture.requestedExtent = request.extent;
        renderTexture.format = texture.format;
        renderTexture.extent = texture.extent;
        ++viewportFramesRendered_;
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

    ImGuiTextureRegistryStats EditorViewportCoordinator::textureRegistryStats() const {
        return textureRegistry_.stats();
    }

    void EditorViewportCoordinator::promotePendingTexture() {
        if (!pendingTexture_.ready()) {
            return;
        }
        if (presentedTexture_.ready()) {
            retiredTextures_.push_back(std::move(presentedTexture_));
        }
        presentedTexture_ = std::move(pendingTexture_);
        pendingTexture_ = {};
    }

    bool EditorViewportCoordinator::hasTextureToRelease() const {
        return presentedTexture_.ready() || pendingTexture_.ready() || !textureRegistry_.empty() ||
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
            texture.clearMetadata();
        }
        retiredTextures_.clear();
        return {};
    }

} // namespace asharia::editor

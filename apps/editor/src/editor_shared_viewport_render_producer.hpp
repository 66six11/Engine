#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"
#include "asharia/rhi_vulkan/vulkan_external_semaphore.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

#include "editor_shared_viewport_external_image_handle_family.hpp"
#include "editor_shared_viewport_external_image_pool.hpp"
#include "editor_shared_viewport_frame_epoch.hpp"
#include "editor_viewport.hpp"

namespace asharia {
    class VulkanContext;
}

namespace asharia::editor {

    struct EditorSharedViewportDebugProxy {
        std::array<std::uint64_t, 2> objectId{};
        std::array<float, 3> position{};
        std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
        std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    };

    struct EditorSharedViewportPresentDesc {
        std::string_view panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D logicalExtent;
        EditorExtent2D allocationExtent;
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
            EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
        bool hasScene{};
        std::uint64_t sceneRevision{};
        std::array<std::uint64_t, 2> sessionId{};
        std::array<std::uint64_t, 2> targetId{};
        std::uint64_t requestSequence{};
        bool hasCamera{};
        EditorViewportCamera camera;
        std::span<const EditorSharedViewportDebugProxy> debugProxies;
        bool flashSentinelCorners{};
    };

    struct EditorSharedViewportPresentPacket {
        void* nativePacket{};
        void* imageHandle{};
        void* waitSemaphoreHandle{};
        void* signalSemaphoreHandle{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D allocationExtent{};
        std::uint64_t memorySizeBytes{};
        std::uint64_t frameIndex{};
    };

    struct EditorSharedViewportRenderProducerStats {
        std::uint64_t framesRendered{};
        std::uint64_t packetsCreated{};
        std::uint64_t rendererCreations{};
        std::uint64_t externalImagesAcquired{};
        std::uint64_t externalImagesCreated{};
        std::uint64_t externalImagesReused{};
        std::uint64_t externalImagesReleased{};
        std::uint64_t externalImagesAvailable{};
        std::uint64_t externalImagesLeased{};
        std::uint64_t frameEpochsSubmitted{};
        std::uint64_t frameEpochsCompleted{};
        std::uint64_t frameEpochsPending{};
        std::uint64_t sceneFramesRendered{};
        std::uint64_t gameFramesRendered{};
        std::uint64_t previewFramesRendered{};
        std::uint64_t lastSceneRevision{};
        std::uint64_t lastRequestSequence{};
        std::array<std::uint64_t, 2> lastSessionId{};
        std::array<std::uint64_t, 2> lastTargetId{};
        EditorViewportKind lastRenderKind{EditorViewportKind::Scene};
        VkExtent2D lastRenderExtent{};
        std::uint32_t lastDebugProxyCount{};
        std::uint64_t lastDebugWorldLineCount{};
        bool lastWorldGridEnabled{};
    };

    struct EditorSharedViewportPacketState final {
    public:
        EditorSharedViewportPacketState() = default;
        EditorSharedViewportPacketState(const EditorSharedViewportPacketState&) = delete;
        EditorSharedViewportPacketState& operator=(const EditorSharedViewportPacketState&) = delete;
        EditorSharedViewportPacketState(EditorSharedViewportPacketState&&) = delete;
        EditorSharedViewportPacketState& operator=(EditorSharedViewportPacketState&&) = delete;
        ~EditorSharedViewportPacketState();

        [[nodiscard]] EditorSharedViewportPresentPacket toPresentPacket();
        [[nodiscard]] Result<void> submitConsumerReleaseWait(VkQueue graphicsQueue);
        [[nodiscard]] Result<bool> retireCompletedGpuWork();
        [[nodiscard]] bool hasPendingGpuWork() const noexcept;
        void abandonPendingGpuWork() noexcept;

        static void closeHandle(void*& handle);

        VkDevice device{VK_NULL_HANDLE};
        VkCommandPool commandPool{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkFence consumerReleaseFence{VK_NULL_HANDLE};
        bool submitted{false};
        bool consumerReleasePending{false};
        bool consumerReleaseSubmitted{false};
        bool reusable{false};
        bool waitForCompositionRelease{false};
        EditorSharedViewportFrameEpochLease frameEpoch;
        std::optional<BasicRenderFrameResourceContext> frameResources;
        VulkanTransientImagePool transientImagePool;
        std::vector<VulkanTransientImageResource> transientImages;
        EditorSharedViewportExternalImageLease imageLease;
        VulkanExternalSemaphore waitSemaphore;
        VulkanExternalSemaphore signalSemaphore;
        void* imageHandle{};
        void* waitSemaphoreHandle{};
        void* signalSemaphoreHandle{};
        std::uint64_t frameIndex{};
        VkExtent2D renderExtent{};
    };

    class EditorSharedViewportRenderProducer final {
    public:
        EditorSharedViewportRenderProducer() = default;
        EditorSharedViewportRenderProducer(const EditorSharedViewportRenderProducer&) = delete;
        EditorSharedViewportRenderProducer&
        operator=(const EditorSharedViewportRenderProducer&) = delete;
        EditorSharedViewportRenderProducer(EditorSharedViewportRenderProducer&&) noexcept = default;
        EditorSharedViewportRenderProducer&
        operator=(EditorSharedViewportRenderProducer&&) noexcept = default;
        ~EditorSharedViewportRenderProducer() = default;

        [[nodiscard]] static Result<EditorSharedViewportRenderProducer>
        create(const VulkanContext& context);

        [[nodiscard]] Result<std::unique_ptr<EditorSharedViewportPacketState>>
        renderSceneViewFrame(BasicRenderViewFrameParams frameParams,
                             EditorSharedViewportPresentDesc desc, std::size_t frameResourceIndex);
        [[nodiscard]] Result<std::unique_ptr<EditorSharedViewportPacketState>>
        createPresentSlot(BasicRenderViewFrameParams frameParams,
                          EditorSharedViewportPresentDesc desc, std::size_t frameResourceIndex);
        [[nodiscard]] Result<void> renderPresentSlot(EditorSharedViewportPacketState& state,
                                                     EditorSharedViewportPresentDesc desc,
                                                     BasicRenderViewFrameParams frameParams);

        [[nodiscard]] EditorSharedViewportRenderProducerStats stats() const;

    private:
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VkQueue graphicsQueue_{VK_NULL_HANDLE};
        std::uint32_t graphicsQueueFamily_{};
        EditorSharedViewportRenderProducerStats stats_;
        EditorSharedViewportExternalImagePool externalImagePool_;
        EditorSharedViewportFrameEpochTracker frameEpochTracker_;
        BasicFullscreenTextureRenderer renderer_;
    };

} // namespace asharia::editor

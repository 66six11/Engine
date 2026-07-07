#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"
#include "asharia/rhi_vulkan/vulkan_external_semaphore.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"

#include "editor_shared_viewport_external_image_handle_family.hpp"
#include "editor_shared_viewport_external_image_pool.hpp"
#include "editor_viewport.hpp"

namespace asharia {
    class VulkanContext;
}

namespace asharia::editor {

    struct EditorSharedViewportPresentDesc {
        std::string_view panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D extent;
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
            EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
    };

    struct EditorSharedViewportPresentPacket {
        void* nativePacket{};
        void* imageHandle{};
        void* waitSemaphoreHandle{};
        void* signalSemaphoreHandle{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
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
    };

    struct EditorSharedViewportPacketState final {
    public:
        EditorSharedViewportPacketState() = default;
        EditorSharedViewportPacketState(const EditorSharedViewportPacketState&) = delete;
        EditorSharedViewportPacketState& operator=(const EditorSharedViewportPacketState&) =
            delete;
        EditorSharedViewportPacketState(EditorSharedViewportPacketState&&) = delete;
        EditorSharedViewportPacketState& operator=(EditorSharedViewportPacketState&&) = delete;
        ~EditorSharedViewportPacketState();

        [[nodiscard]] EditorSharedViewportPresentPacket toPresentPacket();

        static void closeHandle(void*& handle);

        VkDevice device{VK_NULL_HANDLE};
        VkCommandPool commandPool{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        bool submitted{false};
        BasicFullscreenTextureRenderer renderer;
        EditorSharedViewportExternalImageLease imageLease;
        VulkanExternalSemaphore waitSemaphore;
        VulkanExternalSemaphore signalSemaphore;
        void* imageHandle{};
        void* waitSemaphoreHandle{};
        void* signalSemaphoreHandle{};
        std::uint64_t frameIndex{};
    };

    class EditorSharedViewportRenderProducer final {
    public:
        EditorSharedViewportRenderProducer() = default;
        EditorSharedViewportRenderProducer(const EditorSharedViewportRenderProducer&) = delete;
        EditorSharedViewportRenderProducer& operator=(const EditorSharedViewportRenderProducer&) =
            delete;
        EditorSharedViewportRenderProducer(EditorSharedViewportRenderProducer&&) noexcept =
            default;
        EditorSharedViewportRenderProducer&
        operator=(EditorSharedViewportRenderProducer&&) noexcept = default;
        ~EditorSharedViewportRenderProducer() = default;

        [[nodiscard]] static Result<EditorSharedViewportRenderProducer>
        create(const VulkanContext& context);

        [[nodiscard]] Result<std::unique_ptr<EditorSharedViewportPacketState>>
        renderSceneViewFrame(EditorSharedViewportPresentDesc desc, std::uint64_t frameIndex);

        [[nodiscard]] EditorSharedViewportRenderProducerStats stats() const;

    private:
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VkQueue graphicsQueue_{VK_NULL_HANDLE};
        std::uint32_t graphicsQueueFamily_{};
        EditorSharedViewportRenderProducerStats stats_;
        EditorSharedViewportExternalImagePool externalImagePool_;
    };

} // namespace asharia::editor

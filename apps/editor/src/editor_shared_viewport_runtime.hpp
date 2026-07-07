#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "editor_viewport.hpp"

#include "asharia/rhi_vulkan/vulkan_context.hpp"

namespace asharia::editor {

    struct EditorSharedViewportPresentDesc {
        std::string_view panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D extent;
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

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<const asharia::VulkanContext*> ensureContext();
        [[nodiscard]] asharia::Result<EditorSharedViewportPresentPacket>
        renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
        void releasePresentPacket(void* nativePacket);
        void shutdown();

    private:
        [[nodiscard]] std::optional<asharia::VulkanContext>
        takeContextForShutdownIfIdleLocked();

        std::mutex mutex_;
        std::optional<asharia::VulkanContext> context_;
        std::unordered_set<void*> outstandingPackets_;
        std::size_t releasingPacketCount_{};
        std::uint64_t nextFrameIndex_{};
        bool shutdownRequested_{};
    };

} // namespace asharia::editor

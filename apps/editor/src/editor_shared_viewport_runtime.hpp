#pragma once

#include <mutex>
#include <optional>

#include "asharia/rhi_vulkan/vulkan_context.hpp"

namespace asharia::editor {

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<const asharia::VulkanContext*> ensureContext();
        void shutdown();

    private:
        std::mutex mutex_;
        std::optional<asharia::VulkanContext> context_;
    };

} // namespace asharia::editor

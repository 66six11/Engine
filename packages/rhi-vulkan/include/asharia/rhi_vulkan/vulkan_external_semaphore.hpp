#pragma once

#include <vulkan/vulkan.h>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"

namespace asharia {

    struct VulkanExternalSemaphoreDesc {
        VkDevice device{VK_NULL_HANDLE};
    };

    class VulkanExternalSemaphore {
    public:
        VulkanExternalSemaphore() = default;
        VulkanExternalSemaphore(const VulkanExternalSemaphore&) = delete;
        VulkanExternalSemaphore& operator=(const VulkanExternalSemaphore&) = delete;
        VulkanExternalSemaphore(VulkanExternalSemaphore&& other) noexcept;
        VulkanExternalSemaphore& operator=(VulkanExternalSemaphore&& other) noexcept;
        ~VulkanExternalSemaphore();

        [[nodiscard]] static Result<VulkanExternalSemaphore>
        create(const VulkanExternalSemaphoreDesc& desc);
        [[nodiscard]] Result<VulkanExportedOpaqueWin32Handle> exportOpaqueWin32Handle() const;
        [[nodiscard]] VkSemaphore handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkSemaphore semaphore_{VK_NULL_HANDLE};
    };

} // namespace asharia

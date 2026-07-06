#include "asharia/rhi_vulkan/vulkan_external_semaphore.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include <utility>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia {

    namespace {

        [[nodiscard]] PFN_vkGetSemaphoreWin32HandleKHR
        loadGetSemaphoreWin32Handle(VkDevice device) {
            // Vulkan extension entry points are discovered as generic function pointers by design.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR"));
        }

    } // namespace

    VulkanExternalSemaphore::VulkanExternalSemaphore(VulkanExternalSemaphore&& other) noexcept {
        *this = std::move(other);
    }

    VulkanExternalSemaphore&
    VulkanExternalSemaphore::operator=(VulkanExternalSemaphore&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        semaphore_ = std::exchange(other.semaphore_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanExternalSemaphore::~VulkanExternalSemaphore() {
        destroy();
    }

    void VulkanExternalSemaphore::destroy() {
        if (semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        semaphore_ = VK_NULL_HANDLE;
    }

    Result<VulkanExternalSemaphore>
    VulkanExternalSemaphore::create(const VulkanExternalSemaphoreDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan external semaphore without a device")};
        }

        VkExportSemaphoreCreateInfo exportInfo{};
        exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &exportInfo;

        VulkanExternalSemaphore semaphore;
        semaphore.device_ = desc.device;

        const VkResult result = vkCreateSemaphore(desc.device, &createInfo, nullptr,
                                                  &semaphore.semaphore_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan external semaphore", result)};
        }

        return semaphore;
    }

    Result<VulkanExportedOpaqueWin32Handle>
    VulkanExternalSemaphore::exportOpaqueWin32Handle() const {
        if (device_ == VK_NULL_HANDLE || semaphore_ == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot export a Win32 handle from an empty Vulkan external semaphore")};
        }

        const PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle =
            loadGetSemaphoreWin32Handle(device_);
        if (getSemaphoreWin32Handle == nullptr) {
            return std::unexpected{
                vulkanError("VK_KHR_external_semaphore_win32 function is not available")};
        }

        VkSemaphoreGetWin32HandleInfoKHR handleInfo{};
        handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        handleInfo.semaphore = semaphore_;
        handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        HANDLE handle = nullptr;
        const VkResult result = getSemaphoreWin32Handle(device_, &handleInfo, &handle);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to export Vulkan external semaphore handle", result)};
        }

        return VulkanExportedOpaqueWin32Handle{.handle = handle};
    }

    VkSemaphore VulkanExternalSemaphore::handle() const {
        return semaphore_;
    }

} // namespace asharia

#include "asharia/rhi_vulkan/vulkan_submission.hpp"

#include <exception>
#include <utility>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia {
    struct VulkanSubmissionState {
        bool submitted{};
        bool completed{};
    };
    bool VulkanSubmissionReceipt::submitted() const noexcept {
        return state_ && state_->submitted;
    }
    bool VulkanSubmissionReceipt::completed() const noexcept {
        return state_ && state_->completed;
    }
    VulkanSubmission::VulkanSubmission(VkDevice device, VkCommandBuffer commands)
        : device_(device), commands_(commands), thread_(std::this_thread::get_id()),
          state_(std::make_shared<VulkanSubmissionState>()) {}
    Result<VulkanSubmission> VulkanSubmission::create(VkDevice device, VkCommandBuffer commands) {
        if (device == VK_NULL_HANDLE || commands == VK_NULL_HANDLE) {
            return std::unexpected{vulkanError("Submission requires a device and command buffer")};
        }
        return VulkanSubmission{device, commands};
    }
    VulkanSubmission::~VulkanSubmission() {
        if (state_ && state_->submitted && !state_->completed) {
            std::terminate();
        }
        release();
    }
    void VulkanSubmission::release() {
        auto callbacks = std::move(retained_);
        for (auto& callback : callbacks) {
            callback();
        }
        retained_.clear();
    }
    VulkanSubmissionReceipt VulkanSubmission::receipt() const {
        VulkanSubmissionReceipt result;
        result.state_ = state_;
        return result;
    }
    bool VulkanSubmission::retain(VkCommandBuffer commands,
                                  VulkanDeferredDeletionCallback callback) {
        if (thread_ != std::this_thread::get_id() || !state_ || state_->submitted ||
            commands != commands_ || !callback) {
            return false;
        }
        retained_.push_back(std::move(callback));
        return true;
    }
    VoidResult VulkanSubmission::submit(VkQueue queue, const VkSubmitInfo2& info, VkFence fence) {
        if (thread_ != std::this_thread::get_id() || !state_ || state_->submitted ||
            queue == VK_NULL_HANDLE || fence == VK_NULL_HANDLE ||
            info.sType != VK_STRUCTURE_TYPE_SUBMIT_INFO_2 || info.commandBufferInfoCount != 1 ||
            info.pCommandBufferInfos == nullptr ||
            info.pCommandBufferInfos->commandBuffer != commands_) {
            return std::unexpected{
                vulkanError("Submission scope does not match the submitted command buffer")};
        }
        const auto status = vkGetFenceStatus(device_, fence);
        if (status != VK_NOT_READY) {
            return std::unexpected{vulkanError("Submission requires an unsignaled fence", status)};
        }
        const auto result = vkQueueSubmit2(queue, 1, &info, fence);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to submit scoped Vulkan work", result)};
        }
        fence_ = fence;
        state_->submitted = true;
        return {};
    }
    Result<bool> VulkanSubmission::poll() {
        if (thread_ != std::this_thread::get_id() || !state_ || !state_->submitted) {
            return std::unexpected{
                vulkanError("Cannot poll an unsubmitted or foreign-thread scope")};
        }
        if (state_->completed) {
            return true;
        }
        const auto status = vkGetFenceStatus(device_, fence_);
        if (status == VK_NOT_READY) {
            return false;
        }
        if (status != VK_SUCCESS) {
            return std::unexpected{vulkanError("Submission fence query failed", status)};
        }
        state_->completed = true;
        release();
        return true;
    }
} // namespace asharia

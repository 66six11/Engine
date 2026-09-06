#pragma once
#include <vulkan/vulkan.h>

#include <memory>
#include <thread>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/deferred_deletion_queue.hpp"

namespace asharia {
    struct VulkanSubmissionState;
    class VulkanSubmission;
    class VulkanSubmissionReceipt final {
    public:
        [[nodiscard]] bool submitted() const noexcept;
        [[nodiscard]] bool completed() const noexcept;

    private:
        friend class VulkanSubmission;
        std::shared_ptr<const VulkanSubmissionState> state_;
    };

    // Host owns the command pool, fence and device; no reset/destruction before poll completes.
    // Single owner thread. Destruction while pending is a contract violation, not a blocking wait.
    class VulkanSubmission final {
    public:
        VulkanSubmission(const VulkanSubmission&) = delete;
        VulkanSubmission& operator=(const VulkanSubmission&) = delete;
        VulkanSubmission(VulkanSubmission&&) noexcept = default;
        VulkanSubmission& operator=(VulkanSubmission&&) = delete;
        ~VulkanSubmission();
        [[nodiscard]] static Result<VulkanSubmission> create(VkDevice device,
                                                             VkCommandBuffer commands);
        [[nodiscard]] bool retain(VkCommandBuffer commands,
                                  VulkanDeferredDeletionCallback callback);
        [[nodiscard]] VoidResult submit(VkQueue queue, const VkSubmitInfo2& info, VkFence fence);
        [[nodiscard]] Result<bool> poll();
        [[nodiscard]] VulkanSubmissionReceipt receipt() const;

    private:
        VulkanSubmission(VkDevice device, VkCommandBuffer commands);
        void release();
        VkDevice device_{};
        VkCommandBuffer commands_{};
        VkFence fence_{};
        std::thread::id thread_;
        std::shared_ptr<VulkanSubmissionState> state_;
        std::vector<VulkanDeferredDeletionCallback> retained_;
    };
} // namespace asharia

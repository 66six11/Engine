#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/deferred_deletion_queue.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"

namespace vke {

    class VulkanFrameLoop;

    struct VulkanFrameLoopDesc {
        std::uint32_t width{1280};
        std::uint32_t height{720};
        VkClearColorValue clearColor{{0.02F, 0.04F, 0.08F, 1.0F}};
    };

    enum class VulkanFrameStatus {
        Presented,
        Suboptimal,
        OutOfDate,
        Recreated,
    };

    struct VulkanFrameRecordContext {
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        std::uint32_t imageIndex{0};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkClearColorValue clearColor{};
        VulkanFrameLoop* frameLoop{};

        [[nodiscard]] bool deferDeletion(VulkanDeferredDeletionCallback callback) const;
        [[nodiscard]] bool beginDebugLabel(std::string_view name) const;
        void endDebugLabel() const;
    };

    struct VulkanDebugLabelStats {
        std::uint64_t regionsBegun{};
        std::uint64_t regionsEnded{};
        bool available{};
    };

    class VulkanDebugLabelScope {
    public:
        VulkanDebugLabelScope() = default;
        VulkanDebugLabelScope(const VulkanDebugLabelScope&) = delete;
        VulkanDebugLabelScope& operator=(const VulkanDebugLabelScope&) = delete;
        VulkanDebugLabelScope(VulkanDebugLabelScope&& other) noexcept;
        VulkanDebugLabelScope& operator=(VulkanDebugLabelScope&& other) noexcept;
        ~VulkanDebugLabelScope();

        [[nodiscard]] static VulkanDebugLabelScope begin(const VulkanFrameRecordContext& context,
                                                         std::string_view name);

    private:
        VulkanFrameLoop* frameLoop_{};
        VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
        bool active_{};
    };

    struct VulkanFrameRecordResult {
        VkPipelineStageFlags2 waitStageMask{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    };

    using VulkanFrameRecordCallback =
        std::function<Result<VulkanFrameRecordResult>(const VulkanFrameRecordContext&)>;

    class VulkanFrameLoop {
    public:
        VulkanFrameLoop() = default;
        VulkanFrameLoop(const VulkanFrameLoop&) = delete;
        VulkanFrameLoop& operator=(const VulkanFrameLoop&) = delete;
        VulkanFrameLoop(VulkanFrameLoop&& other) noexcept;
        VulkanFrameLoop& operator=(VulkanFrameLoop&& other) noexcept;
        ~VulkanFrameLoop();

        [[nodiscard]] static Result<VulkanFrameLoop> create(const VulkanContext& context,
                                                            const VulkanFrameLoopDesc& desc);

        void setTargetExtent(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] Result<VulkanFrameStatus> recreate();
        [[nodiscard]] Result<VulkanFrameStatus> renderFrame();
        [[nodiscard]] Result<VulkanFrameStatus>
        renderFrame(const VulkanFrameRecordCallback& record);
        [[nodiscard]] bool deferDeletion(VulkanDeferredDeletionCallback callback);

        [[nodiscard]] VkFormat format() const;
        [[nodiscard]] VkExtent2D extent() const;
        [[nodiscard]] VulkanDeferredDeletionStats deferredDeletionStats() const;
        [[nodiscard]] VulkanDebugLabelStats debugLabelStats() const;
        [[nodiscard]] std::uint64_t submittedFrameEpoch() const;
        [[nodiscard]] std::uint64_t completedFrameEpoch() const;

    private:
        friend struct VulkanFrameRecordContext;
        friend class VulkanDebugLabelScope;

        struct AcquiredImage {
            std::uint32_t imageIndex{0};
            bool suboptimal{false};
        };

        struct FrameAcquireResult {
            VulkanFrameStatus status{VulkanFrameStatus::Presented};
            bool hasImage{false};
            AcquiredImage image{};
        };

        void destroy();
        [[nodiscard]] Result<VulkanFrameStatus> recreateSwapchain();
        [[nodiscard]] Result<void> recoverAcquiredImageSemaphore();
        [[nodiscard]] Result<FrameAcquireResult> acquireNextImage();
        [[nodiscard]] Result<void> submitRecordedFrame(std::uint32_t imageIndex,
                                                       const VulkanFrameRecordResult& recorded);
        [[nodiscard]] Result<VulkanFrameStatus> presentFrame(std::uint32_t imageIndex,
                                                             bool acquiredSuboptimal);
        [[nodiscard]] Result<VulkanFrameRecordResult> recordClearCommands(std::uint32_t imageIndex);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrameCommands(std::uint32_t imageIndex, const VulkanFrameRecordCallback& record);
        [[nodiscard]] std::uint64_t retireCompletedFrameWork();
        [[nodiscard]] bool beginDebugLabel(VkCommandBuffer commandBuffer, std::string_view name);
        void endDebugLabel(VkCommandBuffer commandBuffer);

        VkDevice device_{VK_NULL_HANDLE};
        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkSurfaceKHR surface_{VK_NULL_HANDLE};
        VkQueue graphicsQueue_{VK_NULL_HANDLE};
        std::uint32_t graphicsQueueFamily_{0};

        VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        VkExtent2D extent_{};
        VkExtent2D targetExtent_{};
        std::vector<VkImage> images_;
        std::vector<VkImageView> imageViews_;

        VkCommandPool commandPool_{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
        VkSemaphore imageAvailable_{VK_NULL_HANDLE};
        std::vector<VkSemaphore> renderFinished_;
        VkFence inFlight_{VK_NULL_HANDLE};
        VulkanDeferredDeletionQueue deferredDeletionQueue_;
        VulkanDebugLabelFunctions debugLabelFunctions_{};
        VulkanDebugLabelStats debugLabelStats_{};
        std::uint64_t submittedFrameEpoch_{};
        std::uint64_t completedFrameEpoch_{};
        VkClearColorValue clearColor_{{0.02F, 0.04F, 0.08F, 1.0F}};
    };

} // namespace vke

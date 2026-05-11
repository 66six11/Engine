#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/deferred_deletion_queue.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"

namespace asharia {

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

    struct VulkanTimestampQueryStats {
        std::uint64_t framesBegun{};
        std::uint64_t framesResolved{};
        std::uint64_t regionsBegun{};
        std::uint64_t regionsEnded{};
        std::uint64_t regionsResolved{};
        std::uint64_t queryReadbacks{};
        double lastFrameMilliseconds{};
        bool available{};
    };

    struct VulkanTimestampRegionTiming {
        std::string name;
        double milliseconds{};
    };

    class VulkanTimestampScope {
    public:
        VulkanTimestampScope() = default;
        VulkanTimestampScope(const VulkanTimestampScope&) = delete;
        VulkanTimestampScope& operator=(const VulkanTimestampScope&) = delete;
        VulkanTimestampScope(VulkanTimestampScope&& other) noexcept;
        VulkanTimestampScope& operator=(VulkanTimestampScope&& other) noexcept;
        ~VulkanTimestampScope();

        [[nodiscard]] static VulkanTimestampScope begin(const VulkanFrameRecordContext& context,
                                                        std::string_view name);

    private:
        VulkanFrameLoop* frameLoop_{};
        VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
        std::uint32_t endQuery_{};
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
        [[nodiscard]] std::uint32_t swapchainImageCount() const;
        [[nodiscard]] VulkanDeferredDeletionStats deferredDeletionStats() const;
        [[nodiscard]] VulkanDebugLabelStats debugLabelStats() const;
        [[nodiscard]] VulkanTimestampQueryStats timestampStats() const;
        [[nodiscard]] std::span<const VulkanTimestampRegionTiming> latestTimestampTimings() const;
        [[nodiscard]] std::uint64_t submittedFrameEpoch() const;
        [[nodiscard]] std::uint64_t completedFrameEpoch() const;

    private:
        friend struct VulkanFrameRecordContext;
        friend class VulkanDebugLabelScope;
        friend class VulkanTimestampScope;

        struct AcquiredImage {
            std::uint32_t imageIndex{0};
            bool suboptimal{false};
        };

        struct PendingTimestampRegion {
            std::string name;
            std::uint32_t beginQuery{};
            std::uint32_t endQuery{};
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
        [[nodiscard]] Result<void> collectCompletedTimestampQueries();
        void resetTimestampQueriesForRecording(VkCommandBuffer commandBuffer);
        void discardRecordedTimestampQueries();
        [[nodiscard]] bool beginDebugLabel(VkCommandBuffer commandBuffer, std::string_view name);
        void endDebugLabel(VkCommandBuffer commandBuffer);
        [[nodiscard]] bool beginTimestampRegion(VkCommandBuffer commandBuffer,
                                                std::string_view name,
                                                std::uint32_t& endQuery);
        void endTimestampRegion(VkCommandBuffer commandBuffer, std::uint32_t endQuery);
        [[nodiscard]] std::uint64_t timestampDelta(std::uint64_t begin,
                                                   std::uint64_t end) const;

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
        VkQueryPool timestampQueryPool_{VK_NULL_HANDLE};
        std::uint32_t timestampQueryCapacity_{};
        std::uint32_t nextTimestampQuery_{};
        std::uint32_t recordedTimestampQueryCount_{};
        std::uint32_t submittedTimestampQueryCount_{};
        std::uint32_t timestampValidBits_{};
        float timestampPeriodNanoseconds_{};
        bool submittedTimestampQueriesPending_{};
        VulkanTimestampQueryStats timestampStats_{};
        std::vector<PendingTimestampRegion> pendingTimestampRegions_;
        std::vector<PendingTimestampRegion> recordedTimestampRegions_;
        std::vector<PendingTimestampRegion> submittedTimestampRegions_;
        std::vector<VulkanTimestampRegionTiming> latestTimestampTimings_;
        std::uint64_t submittedFrameEpoch_{};
        std::uint64_t completedFrameEpoch_{};
        VkClearColorValue clearColor_{{0.02F, 0.04F, 0.08F, 1.0F}};
    };

} // namespace asharia

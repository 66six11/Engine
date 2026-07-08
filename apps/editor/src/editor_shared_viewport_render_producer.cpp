#include "editor_shared_viewport_render_producer.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::editor {
    namespace {

        constexpr VkFormat kSharedViewportFormat = VK_FORMAT_B8G8R8A8_UNORM;

        [[nodiscard]] Result<void> checkVk(VkResult result, std::string_view context) {
            if (result == VK_SUCCESS) {
                return {};
            }

            return std::unexpected{vulkanError(std::string{context}, result)};
        }

        [[nodiscard]] Result<void>
        createCommandResources(VkDevice device, std::uint32_t graphicsQueueFamily,
                               EditorSharedViewportPacketState& state) {
            state.device = device;

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = graphicsQueueFamily;

            auto result = checkVk(vkCreateCommandPool(device, &poolInfo, nullptr,
                                                      &state.commandPool),
                                  "Failed to create shared viewport command pool");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            VkCommandBufferAllocateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            bufferInfo.commandPool = state.commandPool;
            bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            bufferInfo.commandBufferCount = 1;

            result = checkVk(vkAllocateCommandBuffers(device, &bufferInfo, &state.commandBuffer),
                             "Failed to allocate shared viewport command buffer");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            result = checkVk(vkCreateFence(device, &fenceInfo, nullptr, &state.fence),
                             "Failed to create shared viewport fence");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            return {};
        }

        [[nodiscard]] Result<BasicRenderViewKind> basicRenderViewKind(EditorViewportKind kind) {
            switch (kind) {
            case EditorViewportKind::Scene:
                return BasicRenderViewKind::Scene;
            case EditorViewportKind::Game:
                return BasicRenderViewKind::Game;
            case EditorViewportKind::Preview:
                return BasicRenderViewKind::Preview;
            }

            return std::unexpected{vulkanError("Unknown shared viewport kind")};
        }

        [[nodiscard]] Result<void> recordSharedViewportFrame(
            VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
            std::uint32_t graphicsQueueFamily,
            EditorSharedViewportFrameEpochTracker& frameEpochTracker,
            EditorSharedViewportExternalImagePool& externalImagePool,
            EditorSharedViewportPacketState& state, EditorSharedViewportPresentDesc desc,
            std::uint64_t frameIndex) {
            auto renderer = BasicFullscreenTextureRenderer::create(
                BasicFullscreenTextureRendererDesc{
                    .device = device,
                    .allocator = allocator,
                    .shaderDirectory = ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR,
                });
            if (!renderer) {
                return std::unexpected{std::move(renderer.error())};
            }
            state.renderer = std::move(*renderer);

            auto imageLease = externalImagePool.acquire(
                desc.imageHandleFamily,
                VulkanExternalImageDesc{
                    .device = device,
                    .allocator = allocator,
                    .format = kSharedViewportFormat,
                    .extent = VkExtent2D{.width = desc.extent.width, .height = desc.extent.height},
                    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                });
            if (!imageLease) {
                return std::unexpected{std::move(imageLease.error())};
            }
            state.imageLease = std::move(*imageLease);

            VulkanExternalImage& targetImage = state.imageLease.image();

            auto waitSemaphore =
                VulkanExternalSemaphore::create(VulkanExternalSemaphoreDesc{.device = device});
            if (!waitSemaphore) {
                return std::unexpected{std::move(waitSemaphore.error())};
            }
            state.waitSemaphore = std::move(*waitSemaphore);

            auto signalSemaphore =
                VulkanExternalSemaphore::create(VulkanExternalSemaphoreDesc{.device = device});
            if (!signalSemaphore) {
                return std::unexpected{std::move(signalSemaphore.error())};
            }
            state.signalSemaphore = std::move(*signalSemaphore);

            auto commandResources = createCommandResources(device, graphicsQueueFamily, state);
            if (!commandResources) {
                return std::unexpected{std::move(commandResources.error())};
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            auto result = checkVk(vkBeginCommandBuffer(state.commandBuffer, &beginInfo),
                                  "Failed to begin shared viewport command buffer");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            const VulkanFrameRecordContext frame{
                .commandBuffer = state.commandBuffer,
                .image = targetImage.image(),
                .imageView = targetImage.imageView(),
                .imageIndex = 0U,
                .format = targetImage.format(),
                .extent = targetImage.extent(),
                .clearColor = VkClearColorValue{{0.12F, 0.12F, 0.13F, 1.0F}},
                .frameLoop = nullptr,
            };

            auto viewKind = basicRenderViewKind(desc.kind);
            if (!viewKind) {
                return std::unexpected{std::move(viewKind.error())};
            }

            BasicRenderViewDesc view;
            view.target = BasicRenderViewTarget{
                .image = targetImage.image(),
                .imageView = targetImage.imageView(),
                .format = targetImage.format(),
                .extent = targetImage.extent(),
                .aspectMask = targetImage.aspectMask(),
                .finalUsage = BasicRenderViewTargetFinalUsage::SampledTexture,
            };
            view.viewKind = *viewKind;
            view.frameParams = BasicRenderViewFrameParams{
                .frameIndex = frameIndex,
                .timeSeconds = static_cast<float>(frameIndex) / 60.0F,
                .deltaSeconds = 1.0F / 60.0F,
                .renderScale = 1.0F,
            };
            view.viewName = desc.panelId.empty() ? "Studio Scene View" : desc.panelId;

            auto recorded = state.renderer.recordViewFrame(frame, view);
            if (!recorded) {
                const VkResult endedAfterFailure = vkEndCommandBuffer(state.commandBuffer);
                if (endedAfterFailure != VK_SUCCESS) {
                    logError("Shared viewport command buffer could not end after record failure.");
                }
                return std::unexpected{std::move(recorded.error())};
            }

            result = checkVk(vkEndCommandBuffer(state.commandBuffer),
                             "Failed to end shared viewport command buffer");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            VkCommandBufferSubmitInfo commandInfo{};
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandInfo.commandBuffer = state.commandBuffer;

            VkSemaphoreSubmitInfo signalInfo{};
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfo.semaphore = state.waitSemaphore.handle();
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandInfo;
            submitInfo.signalSemaphoreInfoCount = 1;
            submitInfo.pSignalSemaphoreInfos = &signalInfo;

            result = checkVk(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, state.fence),
                             "Failed to submit shared viewport frame");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }
            state.frameEpoch = frameEpochTracker.submit();
            state.submitted = true;
            state.frameIndex = frameIndex;

            auto imageHandle = targetImage.exportOpaqueWin32Handle();
            if (!imageHandle) {
                return std::unexpected{std::move(imageHandle.error())};
            }
            state.imageHandle = imageHandle->handle;

            auto waitHandle = state.waitSemaphore.exportOpaqueWin32Handle();
            if (!waitHandle) {
                return std::unexpected{std::move(waitHandle.error())};
            }
            state.waitSemaphoreHandle = waitHandle->handle;

            auto signalHandle = state.signalSemaphore.exportOpaqueWin32Handle();
            if (!signalHandle) {
                return std::unexpected{std::move(signalHandle.error())};
            }
            state.signalSemaphoreHandle = signalHandle->handle;

            return {};
        }

    } // namespace

    EditorSharedViewportPacketState::~EditorSharedViewportPacketState() {
        if (submitted && device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
            // Packet release is a compositor/native ownership boundary. Waiting here keeps
            // command-buffer resources alive without stalling the render submit path.
            const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            if (waited != VK_SUCCESS) {
                logError("Shared viewport packet fence wait failed during release.");
                frameEpoch.abandon();
            } else {
                frameEpoch.complete();
            }
        }

        closeHandle(imageHandle);
        closeHandle(waitSemaphoreHandle);
        closeHandle(signalSemaphoreHandle);

        if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
    }

    EditorSharedViewportPresentPacket
    EditorSharedViewportPacketState::toPresentPacket() {
        VulkanExternalImage& targetImage = imageLease.image();
        return EditorSharedViewportPresentPacket{
            .nativePacket = this,
            .imageHandle = imageHandle,
            .waitSemaphoreHandle = waitSemaphoreHandle,
            .signalSemaphoreHandle = signalSemaphoreHandle,
            .format = targetImage.format(),
            .extent = targetImage.extent(),
            .memorySizeBytes = targetImage.memorySizeBytes(),
            .frameIndex = frameIndex,
        };
    }

    void EditorSharedViewportPacketState::closeHandle(void*& handle) {
        if (handle == nullptr) {
            return;
        }
        CloseHandle(static_cast<HANDLE>(handle));
        handle = nullptr;
    }

    Result<EditorSharedViewportRenderProducer>
    EditorSharedViewportRenderProducer::create(const VulkanContext& context) {
        EditorSharedViewportRenderProducer producer;
        producer.device_ = context.device();
        producer.allocator_ = context.allocator();
        producer.graphicsQueue_ = context.graphicsQueue();
        producer.graphicsQueueFamily_ = context.graphicsQueueFamily();
        return producer;
    }

    Result<std::unique_ptr<EditorSharedViewportPacketState>>
    EditorSharedViewportRenderProducer::renderSceneViewFrame(
        EditorSharedViewportPresentDesc desc, std::uint64_t frameIndex) {
        auto state = std::make_unique<EditorSharedViewportPacketState>();
        auto rendered = recordSharedViewportFrame(device_, allocator_, graphicsQueue_,
                                                  graphicsQueueFamily_, frameEpochTracker_,
                                                  externalImagePool_, *state, desc, frameIndex);
        if (!rendered) {
            return std::unexpected{std::move(rendered.error())};
        }

        ++stats_.framesRendered;
        ++stats_.packetsCreated;
        ++stats_.rendererCreations;
        return state;
    }

    EditorSharedViewportRenderProducerStats EditorSharedViewportRenderProducer::stats() const {
        EditorSharedViewportRenderProducerStats snapshot = stats_;
        const EditorSharedViewportFrameEpochStats epochStats = frameEpochTracker_.stats();
        const EditorSharedViewportExternalImagePoolStats poolStats = externalImagePool_.stats();
        snapshot.frameEpochsSubmitted = epochStats.submitted;
        snapshot.frameEpochsCompleted = epochStats.completed;
        snapshot.frameEpochsPending = epochStats.pending;
        snapshot.externalImagesAcquired = poolStats.acquired;
        snapshot.externalImagesCreated = poolStats.created;
        snapshot.externalImagesReused = poolStats.reused;
        snapshot.externalImagesReleased = poolStats.released;
        snapshot.externalImagesAvailable = poolStats.available;
        snapshot.externalImagesLeased = poolStats.leased;
        return snapshot;
    }

} // namespace asharia::editor

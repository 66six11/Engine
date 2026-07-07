#include "editor_shared_viewport_runtime.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <expected>
#include <memory>
#include <mutex>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"
#include "asharia/rhi_vulkan/vulkan_external_semaphore.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

namespace asharia::editor {
    namespace {

        constexpr VkFormat kSharedViewportFormat = VK_FORMAT_B8G8R8A8_UNORM;

        struct EditorSharedViewportPacketState {
            VkDevice device{VK_NULL_HANDLE};
            VkCommandPool commandPool{VK_NULL_HANDLE};
            VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
            VkFence fence{VK_NULL_HANDLE};
            bool submitted{false};
            BasicFullscreenTextureRenderer renderer;
            VulkanExternalImage image;
            VulkanExternalSemaphore waitSemaphore;
            VulkanExternalSemaphore signalSemaphore;
            void* imageHandle{};
            void* waitSemaphoreHandle{};
            void* signalSemaphoreHandle{};

            EditorSharedViewportPacketState() = default;
            EditorSharedViewportPacketState(const EditorSharedViewportPacketState&) = delete;
            EditorSharedViewportPacketState& operator=(const EditorSharedViewportPacketState&) =
                delete;
            EditorSharedViewportPacketState(EditorSharedViewportPacketState&&) = delete;
            EditorSharedViewportPacketState& operator=(EditorSharedViewportPacketState&&) =
                delete;

            ~EditorSharedViewportPacketState() {
                if (submitted && device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
                    // Packet release is a compositor/native ownership boundary. Waiting here keeps
                    // command-buffer resources alive without stalling the render submit path.
                    const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
                    if (waited != VK_SUCCESS) {
                        logError("Shared viewport packet fence wait failed during release.");
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

            static void closeHandle(void*& handle) {
                if (handle == nullptr) {
                    return;
                }
                CloseHandle(static_cast<HANDLE>(handle));
                handle = nullptr;
            }
        };

        [[nodiscard]] Result<void> checkVk(VkResult result, std::string_view context) {
            if (result == VK_SUCCESS) {
                return {};
            }

            return std::unexpected{vulkanError(std::string{context}, result)};
        }

        [[nodiscard]] Result<void>
        createCommandResources(const VulkanContext& context,
                               EditorSharedViewportPacketState& state) {
            state.device = context.device();

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = context.graphicsQueueFamily();

            auto result =
                checkVk(vkCreateCommandPool(state.device, &poolInfo, nullptr, &state.commandPool),
                        "Failed to create shared viewport command pool");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            VkCommandBufferAllocateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            bufferInfo.commandPool = state.commandPool;
            bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            bufferInfo.commandBufferCount = 1;

            result = checkVk(vkAllocateCommandBuffers(state.device, &bufferInfo,
                                                      &state.commandBuffer),
                             "Failed to allocate shared viewport command buffer");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            result = checkVk(vkCreateFence(state.device, &fenceInfo, nullptr, &state.fence),
                             "Failed to create shared viewport fence");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }

            return {};
        }

        [[nodiscard]] Result<BasicRenderViewKind>
        basicRenderViewKind(EditorViewportKind kind) {
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
            const VulkanContext& context, EditorSharedViewportPacketState& state,
            EditorSharedViewportPresentDesc desc, std::uint64_t frameIndex) {
            auto renderer = BasicFullscreenTextureRenderer::create(
                BasicFullscreenTextureRendererDesc{
                    .device = context.device(),
                    .allocator = context.allocator(),
                    .shaderDirectory = ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR,
                });
            if (!renderer) {
                return std::unexpected{std::move(renderer.error())};
            }
            state.renderer = std::move(*renderer);

            auto image = VulkanExternalImage::create(VulkanExternalImageDesc{
                .device = context.device(),
                .allocator = context.allocator(),
                .format = kSharedViewportFormat,
                .extent = VkExtent2D{.width = desc.extent.width, .height = desc.extent.height},
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            });
            if (!image) {
                return std::unexpected{std::move(image.error())};
            }
            state.image = std::move(*image);

            auto waitSemaphore = VulkanExternalSemaphore::create(
                VulkanExternalSemaphoreDesc{.device = context.device()});
            if (!waitSemaphore) {
                return std::unexpected{std::move(waitSemaphore.error())};
            }
            state.waitSemaphore = std::move(*waitSemaphore);

            auto signalSemaphore = VulkanExternalSemaphore::create(
                VulkanExternalSemaphoreDesc{.device = context.device()});
            if (!signalSemaphore) {
                return std::unexpected{std::move(signalSemaphore.error())};
            }
            state.signalSemaphore = std::move(*signalSemaphore);

            auto commandResources = createCommandResources(context, state);
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
                .image = state.image.image(),
                .imageView = state.image.imageView(),
                .imageIndex = 0U,
                .format = state.image.format(),
                .extent = state.image.extent(),
                .clearColor = VkClearColorValue{{0.12F, 0.12F, 0.13F, 1.0F}},
                .frameLoop = nullptr,
            };

            auto viewKind = basicRenderViewKind(desc.kind);
            if (!viewKind) {
                return std::unexpected{std::move(viewKind.error())};
            }

            BasicRenderViewDesc view;
            view.target = BasicRenderViewTarget{
                .image = state.image.image(),
                .imageView = state.image.imageView(),
                .format = state.image.format(),
                .extent = state.image.extent(),
                .aspectMask = state.image.aspectMask(),
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

            result = checkVk(vkQueueSubmit2(context.graphicsQueue(), 1, &submitInfo, state.fence),
                             "Failed to submit shared viewport frame");
            if (!result) {
                return std::unexpected{std::move(result.error())};
            }
            state.submitted = true;

            auto imageHandle = state.image.exportOpaqueWin32Handle();
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

        [[nodiscard]] asharia::Result<void>
        ensureSharedContextStorage(std::optional<asharia::VulkanContext>& contextStorage) {
            if (contextStorage) {
                return {};
            }

            auto context = asharia::VulkanContext::create(asharia::VulkanContextDesc{
                .applicationName = "Asharia Studio Shared Viewport",
                .requiredInstanceExtensions = {},
                .createSurface = {},
                .enableValidation = true,
                .debugLabels = asharia::VulkanDebugLabelMode::Optional,
                .requireVulkan14 = true,
                .externalInterop =
                    asharia::VulkanExternalInteropOptions{
                        .opaqueWin32Memory = true,
                        .opaqueWin32Semaphore = true,
                    },
            });
            if (!context) {
                return std::unexpected{std::move(context.error())};
            }

            contextStorage.emplace(std::move(*context));
            return {};
        }

    } // namespace

    EditorSharedViewportRuntime& EditorSharedViewportRuntime::instance() {
        static EditorSharedViewportRuntime runtime;
        return runtime;
    }

    asharia::Result<const asharia::VulkanContext*> EditorSharedViewportRuntime::ensureContext() {
        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }

        return &*context_;
    }

    asharia::Result<EditorSharedViewportPresentPacket>
    EditorSharedViewportRuntime::renderSceneViewFrame(EditorSharedViewportPresentDesc desc) {
        if (desc.extent.width == 0 || desc.extent.height == 0) {
            return std::unexpected{
                vulkanError("Cannot render a shared viewport frame for an empty extent")};
        }

        std::lock_guard lock{mutex_};
        if (shutdownRequested_) {
            return std::unexpected{vulkanError("Shared viewport runtime has shut down")};
        }

        auto ensured = ensureSharedContextStorage(context_);
        if (!ensured) {
            return std::unexpected{std::move(ensured.error())};
        }

        const std::uint64_t frameIndex = ++nextFrameIndex_;
        auto state = std::make_unique<EditorSharedViewportPacketState>();
        auto rendered = recordSharedViewportFrame(*context_, *state, desc, frameIndex);
        if (!rendered) {
            return std::unexpected{std::move(rendered.error())};
        }

        EditorSharedViewportPacketState* statePtr = state.get();
        outstandingPackets_.insert(statePtr);
        [[maybe_unused]] EditorSharedViewportPacketState* const releasedState = state.release();
        return EditorSharedViewportPresentPacket{
            .nativePacket = statePtr,
            .imageHandle = statePtr->imageHandle,
            .waitSemaphoreHandle = statePtr->waitSemaphoreHandle,
            .signalSemaphoreHandle = statePtr->signalSemaphoreHandle,
            .format = statePtr->image.format(),
            .extent = statePtr->image.extent(),
            .memorySizeBytes = statePtr->image.memorySizeBytes(),
            .frameIndex = frameIndex,
        };
    }

    void EditorSharedViewportRuntime::releasePresentPacket(void* nativePacket) {
        if (nativePacket == nullptr) {
            return;
        }

        std::unique_ptr<EditorSharedViewportPacketState> state;
        {
            std::lock_guard lock{mutex_};
            if (outstandingPackets_.erase(nativePacket) == 0U) {
                return;
            }

            ++releasingPacketCount_;
            state.reset(static_cast<EditorSharedViewportPacketState*>(nativePacket));
        }

        state.reset();

        std::optional<asharia::VulkanContext> contextToDestroy;
        {
            std::lock_guard lock{mutex_};
            --releasingPacketCount_;
            contextToDestroy = takeContextForShutdownIfIdleLocked();
        }
    }

    void EditorSharedViewportRuntime::shutdown() {
        std::optional<asharia::VulkanContext> contextToDestroy;
        {
            std::lock_guard lock{mutex_};
            shutdownRequested_ = true;
            contextToDestroy = takeContextForShutdownIfIdleLocked();
        }
    }

    std::optional<asharia::VulkanContext>
    EditorSharedViewportRuntime::takeContextForShutdownIfIdleLocked() {
        if (!shutdownRequested_ || !outstandingPackets_.empty() || releasingPacketCount_ != 0U ||
            !context_) {
            return std::nullopt;
        }

        std::optional<asharia::VulkanContext> contextToDestroy;
        contextToDestroy.emplace(std::move(*context_));
        context_.reset();
        nextFrameIndex_ = 0U;
        return contextToDestroy;
    }

} // namespace asharia::editor

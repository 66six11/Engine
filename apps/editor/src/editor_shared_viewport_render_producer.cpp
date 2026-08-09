#include "editor_shared_viewport_render_producer.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <windows.h>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::editor {
    namespace {

        constexpr VkFormat kSharedViewportFormat = VK_FORMAT_B8G8R8A8_UNORM;
        [[nodiscard]] std::filesystem::path viewportShaderDirectory() {
            std::array<wchar_t, 32768> executablePath{};
            const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                                    static_cast<DWORD>(executablePath.size()));
            if (length != 0U && length < executablePath.size()) {
                const std::filesystem::path packagedDirectory =
                    std::filesystem::path{std::wstring_view{executablePath.data(), length}}
                        .parent_path() /
                    "shaders" / "renderer-basic";
#if defined(NDEBUG)
                return packagedDirectory;
#else
                if (std::filesystem::exists(packagedDirectory)) {
                    return packagedDirectory;
                }
#endif
            }

#if defined(NDEBUG)
            return {};
#else
            // Debug native smoke executables run directly from the CMake tree.
            return ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR;
#endif
        }
        constexpr std::array<BasicDebugWorldLine, 3> kMinimalSceneAxes{
            BasicDebugWorldLine{
                .start = {0.0F, 0.0F, 0.0F},
                .end = {1.5F, 0.0F, 0.0F},
                .color = {0.92F, 0.18F, 0.18F, 1.0F},
            },
            BasicDebugWorldLine{
                .start = {0.0F, 0.0F, 0.0F},
                .end = {0.0F, 1.5F, 0.0F},
                .color = {0.24F, 0.82F, 0.32F, 1.0F},
            },
            BasicDebugWorldLine{
                .start = {0.0F, 0.0F, 0.0F},
                .end = {0.0F, 0.0F, 1.5F},
                .color = {0.22F, 0.42F, 0.94F, 1.0F},
            },
        };

        [[nodiscard]] constexpr std::array<float, 3> add(std::array<float, 3> lhs,
                                                         std::array<float, 3> rhs) {
            return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
        }

        [[nodiscard]] constexpr std::array<float, 3> multiply(std::array<float, 3> value,
                                                              float scalar) {
            return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
        }

        [[nodiscard]] constexpr std::array<float, 3> cross(std::array<float, 3> lhs,
                                                           std::array<float, 3> rhs) {
            return {
                (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
                (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
                (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
            };
        }

        [[nodiscard]] constexpr std::array<float, 3> rotate(std::array<float, 4> rotation,
                                                            std::array<float, 3> value) {
            const std::array<float, 3> imaginary{rotation[0], rotation[1], rotation[2]};
            const std::array<float, 3> twiceCross = multiply(cross(imaginary, value), 2.0F);
            return add(value, add(multiply(twiceCross, rotation[3]), cross(imaginary, twiceCross)));
        }

        void appendDebugProxyAxis(std::vector<BasicDebugWorldLine>& lines,
                                  const EditorSharedViewportDebugProxy& proxy,
                                  std::array<float, 3> axis, float scale,
                                  std::array<float, 4> color) {
            lines.push_back(BasicDebugWorldLine{
                .start = proxy.position,
                .end = add(proxy.position, rotate(proxy.rotation, multiply(axis, scale))),
                .color = color,
            });
        }

        void appendDebugProxyAxes(std::vector<BasicDebugWorldLine>& lines,
                                  const EditorSharedViewportDebugProxy& proxy) {
            appendDebugProxyAxis(lines, proxy, {1.0F, 0.0F, 0.0F}, proxy.scale[0],
                                 {0.92F, 0.18F, 0.18F, 1.0F});
            appendDebugProxyAxis(lines, proxy, {0.0F, 1.0F, 0.0F}, proxy.scale[1],
                                 {0.24F, 0.82F, 0.32F, 1.0F});
            appendDebugProxyAxis(lines, proxy, {0.0F, 0.0F, 1.0F}, proxy.scale[2],
                                 {0.22F, 0.42F, 0.94F, 1.0F});
        }

        [[nodiscard]] bool hasFlashSentinel(EditorSharedViewportPresentDesc desc) {
            return desc.flashSentinelCorners && desc.hasScene &&
                   desc.kind == EditorViewportKind::Scene;
        }

        struct FlashSentinelImageTransition {
            VkPipelineStageFlags2 sourceStage{};
            VkAccessFlags2 sourceAccess{};
            VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
            VkPipelineStageFlags2 destinationStage{};
            VkAccessFlags2 destinationAccess{};
            VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        };

        void recordFlashSentinelImageBarrier(VkCommandBuffer commandBuffer, VkImage image,
                                             FlashSentinelImageTransition transition) {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = transition.sourceStage;
            barrier.srcAccessMask = transition.sourceAccess;
            barrier.dstStageMask = transition.destinationStage;
            barrier.dstAccessMask = transition.destinationAccess;
            barrier.oldLayout = transition.oldLayout;
            barrier.newLayout = transition.newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);
        }

        void recordFlashSentinel(VkCommandBuffer commandBuffer, VkImage image,
                                 VkImageView imageView, VkExtent2D extent) {
            recordFlashSentinelImageBarrier(
                commandBuffer, image,
                FlashSentinelImageTransition{
                    .sourceStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .sourceAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .destinationStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .destinationAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                });

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = extent,
            };
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &colorAttachment;

            const std::uint32_t side = std::min({24U, extent.width, extent.height});
            struct SentinelCorner {
                VkClearColorValue color{};
                VkOffset2D offset{};
            };
            const std::array<SentinelCorner, 4> corners{
                SentinelCorner{
                    .color = VkClearColorValue{{1.0F, 0.0F, 1.0F, 1.0F}},
                    .offset = VkOffset2D{.x = 0, .y = 0},
                },
                SentinelCorner{
                    .color = VkClearColorValue{{0.0F, 1.0F, 0.0F, 1.0F}},
                    .offset =
                        VkOffset2D{.x = static_cast<std::int32_t>(extent.width - side), .y = 0},
                },
                SentinelCorner{
                    .color = VkClearColorValue{{0.0F, 1.0F, 1.0F, 1.0F}},
                    .offset =
                        VkOffset2D{.x = 0, .y = static_cast<std::int32_t>(extent.height - side)},
                },
                SentinelCorner{
                    .color = VkClearColorValue{{1.0F, 1.0F, 0.0F, 1.0F}},
                    .offset = VkOffset2D{.x = static_cast<std::int32_t>(extent.width - side),
                                         .y = static_cast<std::int32_t>(extent.height - side)},
                },
            };

            vkCmdBeginRendering(commandBuffer, &rendering);
            for (const SentinelCorner& corner : corners) {
                VkClearAttachment clear{};
                clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clear.colorAttachment = 0;
                clear.clearValue.color = corner.color;
                const VkClearRect rect{
                    .rect =
                        VkRect2D{
                            .offset = corner.offset,
                            .extent = VkExtent2D{.width = side, .height = side},
                        },
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };
                vkCmdClearAttachments(commandBuffer, 1, &clear, 1, &rect);
            }
            vkCmdEndRendering(commandBuffer);

            recordFlashSentinelImageBarrier(
                commandBuffer, image,
                FlashSentinelImageTransition{
                    .sourceStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .sourceAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .destinationStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .destinationAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                });
        }

        void recordRenderedViewStats(EditorSharedViewportRenderProducerStats& stats,
                                     EditorSharedViewportPresentDesc desc,
                                     const EditorSharedViewportPacketState& state) {
            if (desc.hasScene) {
                switch (desc.kind) {
                case EditorViewportKind::Scene:
                    ++stats.sceneFramesRendered;
                    break;
                case EditorViewportKind::Game:
                    ++stats.gameFramesRendered;
                    break;
                case EditorViewportKind::Preview:
                    ++stats.previewFramesRendered;
                    break;
                }
                stats.lastSceneRevision = desc.sceneRevision;
            }
            stats.lastRequestSequence = desc.requestSequence;
            stats.lastSessionId = desc.sessionId;
            stats.lastTargetId = desc.targetId;
            stats.lastRenderKind = desc.kind;
            stats.lastRenderExtent = state.renderExtent;
            stats.lastDebugProxyCount = static_cast<std::uint32_t>(desc.debugProxies.size());
            stats.lastDebugWorldLineCount = desc.hasScene && desc.kind == EditorViewportKind::Scene
                                                ? 3U + (desc.debugProxies.size() * 3U)
                                                : 0U;
            stats.lastWorldGridEnabled = desc.hasScene && desc.kind == EditorViewportKind::Scene;
        }

        [[nodiscard]] Result<void> checkVk(VkResult result, std::string_view context) {
            if (result == VK_SUCCESS) {
                return {};
            }

            return std::unexpected{vulkanError(std::string{context}, result)};
        }

        [[nodiscard]] Result<void> createCommandResources(VkDevice device,
                                                          std::uint32_t graphicsQueueFamily,
                                                          EditorSharedViewportPacketState& state) {
            state.device = device;

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = graphicsQueueFamily;

            auto result =
                checkVk(vkCreateCommandPool(device, &poolInfo, nullptr, &state.commandPool),
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

        [[nodiscard]] Result<void> initializePresentState(
            VkDevice device, VmaAllocator allocator, std::uint32_t graphicsQueueFamily,
            EditorSharedViewportExternalImagePool& externalImagePool,
            EditorSharedViewportPacketState& state, EditorSharedViewportPresentDesc desc) {
            auto imageLease = externalImagePool.acquire(
                desc.imageHandleFamily,
                VulkanExternalImageDesc{
                    .device = device,
                    .allocator = allocator,
                    .format = kSharedViewportFormat,
                    .extent = VkExtent2D{.width = desc.allocationExtent.width,
                                         .height = desc.allocationExtent.height},
                    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                });
            if (!imageLease) {
                return std::unexpected{std::move(imageLease.error())};
            }
            state.imageLease = std::move(*imageLease);

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

            VulkanExternalImage& targetImage = state.imageLease.image();
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

        [[nodiscard]] Result<void>
        recordSharedViewportFrame(VkQueue graphicsQueue, BasicFullscreenTextureRenderer& renderer,
                                  EditorSharedViewportFrameEpochTracker& frameEpochTracker,
                                  EditorSharedViewportPacketState& state,
                                  EditorSharedViewportPresentDesc desc,
                                  BasicRenderViewFrameParams frameParams) {
            if (!state.frameResources) {
                return std::unexpected{
                    vulkanError("Shared viewport present slot has no frame resources")};
            }
            VulkanExternalImage& targetImage = state.imageLease.image();

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
                // RenderGraph, render area, viewport and projection use the requested logical
                // extent. Studio supplies the same exact physical extent for allocation and
                // logical size; the wider native contract remains valid for non-Studio callers.
                .extent =
                    VkExtent2D{
                        .width = desc.logicalExtent.width,
                        .height = desc.logicalExtent.height,
                    },
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
                // The VkImage allocation may be wider for a generic native caller, while
                // rendering, projection, viewport and scissor use the explicit logical extent.
                .extent = frame.extent,
                .aspectMask = targetImage.aspectMask(),
                .finalUsage = BasicRenderViewTargetFinalUsage::SampledTexture,
            };
            state.renderExtent = view.target.extent;
            view.viewKind = *viewKind;
            view.frameParams = frameParams;
            view.viewName = desc.panelId.empty() ? "Studio Viewport" : desc.panelId;
            std::vector<BasicDebugWorldLine> debugLines;
            if (desc.hasScene) {
                const EditorViewportCamera camera =
                    desc.hasCamera ? editorViewportCameraForExtent(desc.camera, desc.logicalExtent)
                                   : defaultEditorSceneViewCamera(desc.logicalExtent);
                view.camera = BasicRenderViewCamera{
                    .view = camera.view,
                    .projection = camera.projection,
                    .viewProjection = camera.viewProjection,
                    .position = camera.position,
                    .nearPlane = camera.nearPlane,
                    .farPlane = camera.farPlane,
                };
                if (desc.kind == EditorViewportKind::Scene) {
                    debugLines.assign(kMinimalSceneAxes.begin(), kMinimalSceneAxes.end());
                    debugLines.reserve(debugLines.size() + (desc.debugProxies.size() * 3U));
                    for (const EditorSharedViewportDebugProxy& proxy : desc.debugProxies) {
                        appendDebugProxyAxes(debugLines, proxy);
                    }
                }
                view.overlay = BasicRenderViewOverlayDesc{
                    .enabled = desc.kind == EditorViewportKind::Scene,
                    .worldGrid =
                        BasicRenderViewWorldGridDesc{
                            .enabled = desc.kind == EditorViewportKind::Scene,
                            .planeY = 0.0F,
                            .minorSpacing = 1.0F,
                            .majorSpacing = 10.0F,
                            .fadeStart = 12.0F,
                            .fadeEnd = 80.0F,
                            .opacity = 0.72F,
                            .color = {0.36F, 0.39F, 0.44F, 1.0F},
                        },
                    .debugWorldLines = std::span<const BasicDebugWorldLine>{debugLines},
                };
            }

            auto recorded =
                renderer.recordViewFrame(frame, view, *state.frameResources,
                                         state.transientImagePool, state.transientImages);
            if (!recorded) {
                const VkResult endedAfterFailure = vkEndCommandBuffer(state.commandBuffer);
                if (endedAfterFailure != VK_SUCCESS) {
                    logError("Shared viewport command buffer could not end after record failure.");
                }
                return std::unexpected{std::move(recorded.error())};
            }

            if (hasFlashSentinel(desc)) {
                recordFlashSentinel(state.commandBuffer, targetImage.image(),
                                    targetImage.imageView(), view.target.extent);
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

            VkSemaphoreSubmitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = state.signalSemaphore.handle();
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.waitSemaphoreInfoCount = state.waitForCompositionRelease ? 1U : 0U;
            submitInfo.pWaitSemaphoreInfos = state.waitForCompositionRelease ? &waitInfo : nullptr;
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
            state.waitForCompositionRelease = true;
            state.frameIndex = frameParams.frameIndex;

            return {};
        }

    } // namespace

    EditorSharedViewportPacketState::~EditorSharedViewportPacketState() {
        if (hasPendingGpuWork()) {
            logError("Shared viewport packet destruction was attempted before GPU completion.");
            frameEpoch.abandon();
            std::terminate();
        }

        closeHandle(imageHandle);
        closeHandle(waitSemaphoreHandle);
        closeHandle(signalSemaphoreHandle);

        if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (device != VK_NULL_HANDLE && consumerReleaseFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, consumerReleaseFence, nullptr);
        }
        if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
    }

    Result<void> EditorSharedViewportPacketState::submitConsumerReleaseWait(VkQueue graphicsQueue) {
        if (consumerReleasePending) {
            return std::unexpected{
                vulkanError("Shared viewport consumer release wait was already requested")};
        }

        // From this point forward destruction is unsafe until a successful
        // consumer-done wait fence proves that the external reader released
        // the shared image. Failures deliberately leave this state pending so
        // the runtime quarantines the packet instead of returning its image to
        // the cross-packet pool.
        consumerReleasePending = true;
        if (device == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE ||
            signalSemaphore.handle() == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Shared viewport packet cannot wait for consumer release")};
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        auto result = checkVk(vkCreateFence(device, &fenceInfo, nullptr, &consumerReleaseFence),
                              "Failed to create shared viewport consumer release fence");
        if (!result) {
            return std::unexpected{std::move(result.error())};
        }

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = signalSemaphore.handle();
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = 1U;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;

        result = checkVk(vkQueueSubmit2(graphicsQueue, 1U, &submitInfo, consumerReleaseFence),
                         "Failed to submit shared viewport consumer release wait");
        if (!result) {
            return std::unexpected{std::move(result.error())};
        }

        consumerReleaseSubmitted = true;
        return {};
    }

    Result<bool> EditorSharedViewportPacketState::retireCompletedGpuWork() {
        if (submitted) {
            if (device == VK_NULL_HANDLE || fence == VK_NULL_HANDLE) {
                return std::unexpected{
                    vulkanError("Shared viewport present slot has no submission fence")};
            }

            const VkResult status = vkGetFenceStatus(device, fence);
            if (status == VK_NOT_READY) {
                return false;
            }
            if (status != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query shared viewport present slot fence", status)};
            }

            frameEpoch.complete();
            submitted = false;
            for (VulkanTransientImageResource& resource : transientImages) {
                auto released = transientImagePool.releaseCompleted(resource);
                if (!released) {
                    return std::unexpected{std::move(released.error())};
                }
            }
            transientImages.clear();
        }

        if (consumerReleasePending) {
            if (!consumerReleaseSubmitted || device == VK_NULL_HANDLE ||
                consumerReleaseFence == VK_NULL_HANDLE) {
                return std::unexpected{
                    vulkanError("Shared viewport consumer release completion cannot be confirmed")};
            }

            const VkResult status = vkGetFenceStatus(device, consumerReleaseFence);
            if (status == VK_NOT_READY) {
                return false;
            }
            if (status != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query shared viewport consumer release fence", status)};
            }

            vkDestroyFence(device, consumerReleaseFence, nullptr);
            consumerReleaseFence = VK_NULL_HANDLE;
            consumerReleaseSubmitted = false;
            consumerReleasePending = false;
        }

        return true;
    }

    bool EditorSharedViewportPacketState::hasPendingGpuWork() const noexcept {
        return submitted || consumerReleasePending;
    }

    void EditorSharedViewportPacketState::abandonPendingGpuWork() noexcept {
        frameEpoch.abandon();
    }

    EditorSharedViewportPresentPacket EditorSharedViewportPacketState::toPresentPacket() {
        VulkanExternalImage& targetImage = imageLease.image();
        return EditorSharedViewportPresentPacket{
            .nativePacket = this,
            .imageHandle = imageHandle,
            .waitSemaphoreHandle = waitSemaphoreHandle,
            .signalSemaphoreHandle = signalSemaphoreHandle,
            .format = targetImage.format(),
            .allocationExtent = targetImage.extent(),
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
        const std::filesystem::path shaderDirectory = viewportShaderDirectory();
        if (shaderDirectory.empty()) {
            return std::unexpected{
                vulkanError("Could not resolve the packaged shared viewport shader directory")};
        }
        auto renderer = BasicFullscreenTextureRenderer::create(BasicFullscreenTextureRendererDesc{
            .device = producer.device_,
            .allocator = producer.allocator_,
            .shaderDirectory = shaderDirectory,
        });
        if (!renderer) {
            return std::unexpected{std::move(renderer.error())};
        }

        producer.renderer_ = std::move(*renderer);
        ++producer.stats_.rendererCreations;
        return producer;
    }

    Result<std::unique_ptr<EditorSharedViewportPacketState>>
    EditorSharedViewportRenderProducer::renderSceneViewFrame(BasicRenderViewFrameParams frameParams,
                                                             EditorSharedViewportPresentDesc desc,
                                                             std::size_t frameResourceIndex) {
        auto state = std::make_unique<EditorSharedViewportPacketState>();
        auto frameResources = renderer_.createFrameResourceContext(frameResourceIndex);
        if (!frameResources) {
            return std::unexpected{std::move(frameResources.error())};
        }
        state->frameResources.emplace(std::move(*frameResources));
        auto initialized = initializePresentState(device_, allocator_, graphicsQueueFamily_,
                                                  externalImagePool_, *state, desc);
        if (!initialized) {
            return std::unexpected{std::move(initialized.error())};
        }

        auto rendered = recordSharedViewportFrame(graphicsQueue_, renderer_, frameEpochTracker_,
                                                  *state, desc, frameParams);
        if (!rendered) {
            return std::unexpected{std::move(rendered.error())};
        }

        ++stats_.framesRendered;
        ++stats_.packetsCreated;
        recordRenderedViewStats(stats_, desc, *state);
        return state;
    }

    Result<std::unique_ptr<EditorSharedViewportPacketState>>
    EditorSharedViewportRenderProducer::createPresentSlot(BasicRenderViewFrameParams frameParams,
                                                          EditorSharedViewportPresentDesc desc,
                                                          std::size_t frameResourceIndex) {
        auto state = std::make_unique<EditorSharedViewportPacketState>();
        state->reusable = true;
        auto frameResources = renderer_.createFrameResourceContext(frameResourceIndex);
        if (!frameResources) {
            return std::unexpected{std::move(frameResources.error())};
        }
        state->frameResources.emplace(std::move(*frameResources));
        auto initialized = initializePresentState(device_, allocator_, graphicsQueueFamily_,
                                                  externalImagePool_, *state, desc);
        if (!initialized) {
            return std::unexpected{std::move(initialized.error())};
        }

        auto rendered = recordSharedViewportFrame(graphicsQueue_, renderer_, frameEpochTracker_,
                                                  *state, desc, frameParams);
        if (!rendered) {
            return std::unexpected{std::move(rendered.error())};
        }

        ++stats_.framesRendered;
        ++stats_.packetsCreated;
        recordRenderedViewStats(stats_, desc, *state);
        return state;
    }

    Result<void>
    EditorSharedViewportRenderProducer::renderPresentSlot(EditorSharedViewportPacketState& state,
                                                          EditorSharedViewportPresentDesc desc,
                                                          BasicRenderViewFrameParams frameParams) {
        if (!state.reusable) {
            return std::unexpected{
                vulkanError("Shared viewport present packet is not a reusable slot")};
        }
        const VkExtent2D extent = state.imageLease.image().extent();
        if (extent.width != desc.allocationExtent.width ||
            extent.height != desc.allocationExtent.height) {
            return std::unexpected{
                vulkanError("Shared viewport present slot extent cannot change in place")};
        }

        auto retired = state.retireCompletedGpuWork();
        if (!retired) {
            return std::unexpected{std::move(retired.error())};
        }
        if (!*retired) {
            return std::unexpected{
                vulkanError("Shared viewport present slot GPU work is still pending")};
        }

        auto resetFence = checkVk(vkResetFences(device_, 1, &state.fence),
                                  "Failed to reset shared viewport present slot fence");
        if (!resetFence) {
            return std::unexpected{std::move(resetFence.error())};
        }
        auto resetPool = checkVk(vkResetCommandPool(device_, state.commandPool, 0),
                                 "Failed to reset shared viewport present slot command pool");
        if (!resetPool) {
            return std::unexpected{std::move(resetPool.error())};
        }

        auto rendered = recordSharedViewportFrame(graphicsQueue_, renderer_, frameEpochTracker_,
                                                  state, desc, frameParams);
        if (!rendered) {
            return std::unexpected{std::move(rendered.error())};
        }

        ++stats_.framesRendered;
        recordRenderedViewStats(stats_, desc, state);
        return {};
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

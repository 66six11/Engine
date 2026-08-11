#include "scene_mesh_smoke.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/renderer_basic_vulkan/clear_frame.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

namespace asharia::sample_viewer {
    namespace {

        using SmokeVec3 = std::array<float, 3>;
        using SmokeMat4 = std::array<float, 16>;

        constexpr VkExtent2D kReadbackExtent{.width = 320, .height = 240};
        constexpr VkFormat kReadbackFormat = VK_FORMAT_B8G8R8A8_SRGB;
        constexpr VkDeviceSize kBytesPerPixel = 4;
        constexpr std::uint32_t kChangedPixelThreshold = 12;
        constexpr std::size_t kProbeCount = 8;
        constexpr std::size_t kRetainedFormatProbeCount = 3;
        constexpr std::uint64_t kMinimumSolidPixels = 2'500;
        constexpr std::uint64_t kMinimumSolidPixelsPerHalf = 1'200;
        constexpr std::uint64_t kMinimumWirePixels = 750;
        constexpr std::uint64_t kMinimumSolidInteriorPixels = 1'600;
        constexpr std::uint64_t kMinimumMovedRightDifference = 300'000;
        constexpr std::uint64_t kMaximumStationaryLeftDifference = 0;
        constexpr std::uint64_t kMaximumOrderDifference = 0;
        constexpr std::uint64_t kMaximumSameFrameDifference = 0;

        enum class ProbeKind : std::uint8_t {
            Empty,
            Solid,
            Wireframe,
            Moved,
            DepthNearThenFar,
            DepthFarThenNear,
            SameFrameSceneWireframe,
            SameFrameGameSolid,
        };

        struct SceneMeshProbe {
            VulkanRenderTarget target;
            VulkanBuffer readback;
            std::vector<std::byte> pixels;
            BasicRenderViewDiagnostics diagnostics;
            VkFormat targetFormat{kReadbackFormat};
            bool diagnosticsRecorded{};
        };

        struct SmokePixelMetrics {
            std::uint64_t solidPixels{};
            std::uint64_t solidLeftPixels{};
            std::uint64_t solidRightPixels{};
            std::uint64_t wirePixels{};
            std::uint64_t solidInteriorPixels{};
            std::uint64_t clearedSolidInteriorPixels{};
            std::uint64_t movedLeftDifference{};
            std::uint64_t movedRightDifference{};
            std::uint64_t depthOrderDifference{};
            std::uint64_t sameFrameWireDifference{};
            std::uint64_t sameFrameSolidDifference{};
        };

        struct SmokeLookAtDesc {
            SmokeVec3 position{};
            SmokeVec3 target{};
            SmokeVec3 upDirection{};
        };

        struct SmokePerspectiveDesc {
            float verticalFovRadians{};
            float aspectRatio{1.0F};
            float nearPlane{0.1F};
            float farPlane{32.0F};
        };

        struct SceneMeshModelDesc {
            float scale{1.0F};
            float rotationY{};
            SmokeVec3 translation{};
        };

        struct UnknownResourceCheckState {
            bool meshRejected{};
            bool materialRejected{};
        };

        struct HorizontalRegion {
            std::uint32_t firstCoordinate{};
            std::uint32_t pastLastCoordinate{};
        };

        struct MaskComparison {
            std::span<const std::uint8_t> solid;
            std::span<const std::uint8_t> wireframe;
        };

        [[nodiscard]] Error sceneMeshSmokeError(std::string message) {
            return Error{ErrorDomain::Vulkan, 0, std::move(message)};
        }

        [[nodiscard]] constexpr std::size_t probeIndex(ProbeKind kind) {
            return static_cast<std::size_t>(kind);
        }

        [[nodiscard]] constexpr VkDeviceSize readbackByteCount() {
            return static_cast<VkDeviceSize>(kReadbackExtent.width) * kReadbackExtent.height *
                   kBytesPerPixel;
        }

        [[nodiscard]] constexpr SmokeVec3 subtract(SmokeVec3 lhs, SmokeVec3 rhs) {
            return SmokeVec3{lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
        }

        [[nodiscard]] constexpr float dot(SmokeVec3 lhs, SmokeVec3 rhs) {
            return (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]);
        }

        [[nodiscard]] constexpr SmokeVec3 cross(SmokeVec3 lhs, SmokeVec3 rhs) {
            return SmokeVec3{
                (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
                (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
                (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
            };
        }

        [[nodiscard]] SmokeVec3 normalize(SmokeVec3 value) {
            const float length = std::sqrt(dot(value, value));
            if (length <= 0.0F) {
                return SmokeVec3{};
            }
            return SmokeVec3{value[0] / length, value[1] / length, value[2] / length};
        }

        [[nodiscard]] constexpr float mat4At(const SmokeMat4& matrix, std::size_t row,
                                             std::size_t column) {
            return matrix.at((row * 4U) + column);
        }

        [[nodiscard]] SmokeMat4 multiply(SmokeMat4 lhs, SmokeMat4 rhs) {
            SmokeMat4 result{};
            for (std::size_t row = 0; row < 4U; ++row) {
                for (std::size_t column = 0; column < 4U; ++column) {
                    float value = 0.0F;
                    for (std::size_t index = 0; index < 4U; ++index) {
                        value += mat4At(lhs, row, index) * mat4At(rhs, index, column);
                    }
                    result.at((row * 4U) + column) = value;
                }
            }
            return result;
        }

        [[nodiscard]] SmokeMat4 lookAt(const SmokeLookAtDesc& desc) {
            const SmokeVec3 forward = normalize(subtract(desc.target, desc.position));
            const SmokeVec3 right = normalize(cross(desc.upDirection, forward));
            const SmokeVec3 cameraUp = cross(forward, right);
            return SmokeMat4{
                right[0],    right[1],    right[2],    -dot(right, desc.position),
                cameraUp[0], cameraUp[1], cameraUp[2], -dot(cameraUp, desc.position),
                forward[0],  forward[1],  forward[2],  -dot(forward, desc.position),
                0.0F,        0.0F,        0.0F,        1.0F,
            };
        }

        [[nodiscard]] SmokeMat4 perspective(const SmokePerspectiveDesc& desc) {
            const float focalLength = 1.0F / std::tan(desc.verticalFovRadians * 0.5F);
            return SmokeMat4{
                focalLength / desc.aspectRatio,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                focalLength,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                desc.farPlane / (desc.farPlane - desc.nearPlane),
                (-desc.nearPlane * desc.farPlane) / (desc.farPlane - desc.nearPlane),
                0.0F,
                0.0F,
                1.0F,
                0.0F,
            };
        }

        [[nodiscard]] BasicRenderViewCamera sceneMeshCamera() {
            constexpr SmokeVec3 kPosition{0.0F, 8.0F, 3.0F};
            constexpr SmokeVec3 kTarget{0.0F, 0.0F, 3.0F};
            constexpr SmokeVec3 kUp{0.0F, 0.0F, 1.0F};
            constexpr float kNearPlane = 0.1F;
            constexpr float kFarPlane = 32.0F;
            constexpr float kVerticalFov = 45.0F * std::numbers::pi_v<float> / 180.0F;
            constexpr float kAspect =
                static_cast<float>(kReadbackExtent.width) / kReadbackExtent.height;
            const SmokeMat4 view = lookAt(SmokeLookAtDesc{
                .position = kPosition,
                .target = kTarget,
                .upDirection = kUp,
            });
            const SmokeMat4 projection = perspective(SmokePerspectiveDesc{
                .verticalFovRadians = kVerticalFov,
                .aspectRatio = kAspect,
                .nearPlane = kNearPlane,
                .farPlane = kFarPlane,
            });
            return BasicRenderViewCamera{
                .view = view,
                .projection = projection,
                .viewProjection = multiply(projection, view),
                .position = kPosition,
                .nearPlane = kNearPlane,
                .farPlane = kFarPlane,
            };
        }

        [[nodiscard]] BasicTransformMatrix3D sceneMeshModel(const SceneMeshModelDesc& desc) {
            const float cosine = std::cos(desc.rotationY);
            const float sine = std::sin(desc.rotationY);
            return BasicTransformMatrix3D{
                desc.scale * cosine,
                0.0F,
                desc.scale * sine,
                desc.translation[0],
                0.0F,
                desc.scale,
                0.0F,
                desc.translation[1],
                -desc.scale * sine,
                0.0F,
                desc.scale * cosine,
                desc.translation[2],
                0.0F,
                0.0F,
                0.0F,
                1.0F,
            };
        }

        [[nodiscard]] BasicDrawListItem sceneMeshItem(std::uint32_t sourceIndex,
                                                      BasicTransformMatrix3D model) {
            return BasicDrawListItem{
                .drawItem = basicValidationMeshDrawItem(),
                .modelMatrix = model,
                .context =
                    BasicDrawPacketContext{
                        .sourceObject = BasicDrawSourceId{.index = sourceIndex, .generation = 7U},
                        .meshResource = kBasicValidationMeshResourceKey,
                        .materialResource = kBasicDefaultUnlitMaterialResourceKey,
                    },
            };
        }

        [[nodiscard]] std::array<BasicDrawListItem, 2> baseSceneItems() {
            return std::array{
                sceneMeshItem(101U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {-1.55F, 0.0F, 3.0F},
                                    })),
                sceneMeshItem(102U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {1.55F, 0.0F, 3.0F},
                                    })),
            };
        }

        [[nodiscard]] std::array<BasicDrawListItem, 2> movedSceneItems() {
            return std::array{
                sceneMeshItem(101U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {-1.55F, 0.0F, 3.0F},
                                    })),
                sceneMeshItem(102U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {2.30F, 0.0F, 3.0F},
                                    })),
            };
        }

        [[nodiscard]] std::array<BasicDrawListItem, 2> depthSceneItems(bool reverseOrder) {
            BasicDrawListItem nearItem = sceneMeshItem(301U, sceneMeshModel(SceneMeshModelDesc{
                                                                 .scale = 0.90F,
                                                                 .translation = {0.0F, 0.65F, 3.0F},
                                                             }));
            BasicDrawListItem farItem =
                sceneMeshItem(302U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.90F,
                                        .rotationY = std::numbers::pi_v<float>,
                                        .translation = {0.0F, -0.65F, 3.0F},
                                    }));
            if (reverseOrder) {
                return std::array{farItem, nearItem};
            }
            return std::array{nearItem, farItem};
        }

        [[nodiscard]] Result<SceneMeshProbe> createProbe(const VulkanContext& context) {
            auto readback = VulkanBuffer::create(VulkanBufferDesc{
                .device = context.device(),
                .allocator = context.allocator(),
                .size = readbackByteCount(),
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .memoryUsage = VulkanBufferMemoryUsage::HostReadback,
            });
            if (!readback) {
                return std::unexpected{std::move(readback.error())};
            }
            SceneMeshProbe probe;
            probe.readback = std::move(*readback);
            probe.pixels.resize(static_cast<std::size_t>(readbackByteCount()));
            return probe;
        }

        [[nodiscard]] Result<void> recordReadbackCopy(const VulkanFrameRecordContext& frame,
                                                      VkImage image, VkBuffer buffer) {
            if (image == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
                return std::unexpected{
                    sceneMeshSmokeError("Scene mesh smoke readback resources are incomplete")};
            }

            VkImageMemoryBarrier2 imageBarrier{};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = image;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            VkDependencyInfo imageDependency{};
            imageDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            imageDependency.imageMemoryBarrierCount = 1;
            imageDependency.pImageMemoryBarriers = &imageBarrier;
            vkCmdPipelineBarrier2(frame.commandBuffer, &imageDependency);

            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = VkExtent3D{
                .width = kReadbackExtent.width,
                .height = kReadbackExtent.height,
                .depth = 1,
            };
            vkCmdCopyImageToBuffer(frame.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   buffer, 1, &copy);

            VkBufferMemoryBarrier2 bufferBarrier{};
            bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferBarrier.buffer = buffer;
            bufferBarrier.offset = 0;
            bufferBarrier.size = VK_WHOLE_SIZE;

            VkDependencyInfo bufferDependency{};
            bufferDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            bufferDependency.bufferMemoryBarrierCount = 1;
            bufferDependency.pBufferMemoryBarriers = &bufferBarrier;
            vkCmdPipelineBarrier2(frame.commandBuffer, &bufferDependency);
            return {};
        }

        [[nodiscard]] Result<void>
        recordProbeView(const VulkanFrameRecordContext& frame, const VulkanContext& context,
                        BasicFullscreenTextureRenderer& renderer, SceneMeshProbe& probe,
                        std::span<const BasicDrawListItem> items, BasicSceneRasterMode rasterMode,
                        BasicRenderViewKind viewKind, std::uint64_t frameIndex,
                        std::string_view viewName, BasicRenderViewOverlayDesc overlay = {}) {
            auto targetReady = probe.target.ensure(
                frame, VulkanRenderTargetDesc{
                           .device = context.device(),
                           .allocator = context.allocator(),
                           .format = probe.targetFormat,
                           .extent = kReadbackExtent,
                           .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                       });
            if (!targetReady) {
                return std::unexpected{std::move(targetReady.error())};
            }

            const VulkanSampledTextureView sampled = probe.target.sampledTextureView();
            auto recorded = renderer.recordViewFrame(
                frame, BasicRenderViewDesc{
                           .target =
                               BasicRenderViewTarget{
                                   .image = sampled.image,
                                   .imageView = sampled.imageView,
                                   .format = sampled.format,
                                   .extent = sampled.extent,
                                   .aspectMask = sampled.aspectMask,
                                   .finalUsage = BasicRenderViewTargetFinalUsage::SampledTexture,
                               },
                           .viewKind = viewKind,
                           .camera = sceneMeshCamera(),
                           .frameParams = BasicRenderViewFrameParams{.frameIndex = frameIndex},
                           .scene =
                               BasicRenderViewSceneDesc{
                                   .drawItems = items,
                                   .rasterMode = rasterMode,
                               },
                           .overlay = overlay,
                           .viewName = viewName,
                           .diagnostics = &probe.diagnostics,
                       });
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }
            probe.diagnosticsRecorded = true;
            return recordReadbackCopy(frame, sampled.image, probe.readback.handle());
        }

        [[nodiscard]] Result<VulkanFrameRecordResult>
        finishProbeFrame(const VulkanFrameRecordContext& frame) {
            return recordBasicClearFrame(frame);
        }

        template <typename Callback>
        [[nodiscard]] bool submitSmokeFrame(VulkanFrameLoop& frameLoop, GlfwWindow& window,
                                            Callback&& callback, std::string_view context) {
            GlfwWindow::pollEvents();
            const WindowFramebufferExtent extent = window.framebufferExtent();
            frameLoop.setTargetExtent(extent.width, extent.height);
            auto status = frameLoop.renderFrame(std::forward<Callback>(callback));
            if (!status) {
                logError(status.error().message);
                return false;
            }
            if (*status == VulkanFrameStatus::OutOfDate) {
                logError(std::string{context} + " left the swapchain out of date");
                return false;
            }
            return true;
        }

        [[nodiscard]] BasicRenderViewTarget
        swapchainViewTarget(const VulkanFrameRecordContext& frame) {
            return BasicRenderViewTarget{
                .image = frame.image,
                .imageView = frame.imageView,
                .format = frame.format,
                .extent = frame.extent,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .finalUsage = BasicRenderViewTargetFinalUsage::Present,
            };
        }

        [[nodiscard]] bool validateUnknownResourceError(const Error& error,
                                                        std::string_view resourceLabel,
                                                        BasicDrawResourceKey resource,
                                                        BasicDrawSourceId source) {
            const std::string sourceContext = "source object " + std::to_string(source.index) +
                                              ":" + std::to_string(source.generation);
            const std::string resourceContext =
                std::string{resourceLabel} + " " + std::to_string(resource.value);
            return error.domain == ErrorDomain::RenderGraph && error.code == 0 &&
                   error.message.find("item 0") != std::string::npos &&
                   error.message.find(sourceContext) != std::string::npos &&
                   error.message.find(resourceContext) != std::string::npos;
        }

        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordUnknownResourceChecks(const VulkanFrameRecordContext& frame,
                                    BasicFullscreenTextureRenderer& renderer,
                                    UnknownResourceCheckState& state) {
            constexpr BasicDrawResourceKey kUnknownMesh{.value = 0xBAD0000000000001ULL};
            constexpr BasicDrawResourceKey kUnknownMaterial{.value = 0xBAD0000000000002ULL};

            BasicDrawListItem unknownMesh =
                sceneMeshItem(901U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {0.0F, 0.0F, 3.0F},
                                    }));
            unknownMesh.context.meshResource = kUnknownMesh;
            const std::array unknownMeshItems{unknownMesh};
            auto meshResult = renderer.recordViewFrame(
                frame, BasicRenderViewDesc{
                           .target = swapchainViewTarget(frame),
                           .viewKind = BasicRenderViewKind::Scene,
                           .camera = sceneMeshCamera(),
                           .frameParams = BasicRenderViewFrameParams{.frameIndex = 90U},
                           .scene = BasicRenderViewSceneDesc{.drawItems = unknownMeshItems},
                           .overlay = {},
                           .viewName = "RenderViewUnknownMeshSmoke",
                       });
            if (meshResult) {
                return std::unexpected{
                    sceneMeshSmokeError("Scene mesh smoke accepted an unknown mesh key")};
            }
            state.meshRejected =
                meshResult.error().message.find("unresolved mesh resource") != std::string::npos &&
                validateUnknownResourceError(meshResult.error(), "mesh resource", kUnknownMesh,
                                             unknownMesh.context.sourceObject);
            if (!state.meshRejected) {
                return std::unexpected{
                    sceneMeshSmokeError("Scene mesh smoke lost unknown mesh failure context: " +
                                        meshResult.error().message)};
            }

            BasicDrawListItem unknownMaterial =
                sceneMeshItem(902U, sceneMeshModel(SceneMeshModelDesc{
                                        .scale = 0.75F,
                                        .translation = {0.0F, 0.0F, 3.0F},
                                    }));
            unknownMaterial.context.materialResource = kUnknownMaterial;
            const std::array unknownMaterialItems{unknownMaterial};
            auto materialResult = renderer.recordViewFrame(
                frame, BasicRenderViewDesc{
                           .target = swapchainViewTarget(frame),
                           .viewKind = BasicRenderViewKind::Game,
                           .camera = sceneMeshCamera(),
                           .frameParams = BasicRenderViewFrameParams{.frameIndex = 91U},
                           .scene = BasicRenderViewSceneDesc{.drawItems = unknownMaterialItems},
                           .overlay = {},
                           .viewName = "RenderViewUnknownMaterialSmoke",
                       });
            if (materialResult) {
                return std::unexpected{
                    sceneMeshSmokeError("Scene mesh smoke accepted an unknown material key")};
            }
            state.materialRejected =
                materialResult.error().message.find("unresolved material resource") !=
                    std::string::npos &&
                validateUnknownResourceError(materialResult.error(), "material resource",
                                             kUnknownMaterial,
                                             unknownMaterial.context.sourceObject);
            if (!state.materialRejected) {
                return std::unexpected{
                    sceneMeshSmokeError("Scene mesh smoke lost unknown material failure context: " +
                                        materialResult.error().message)};
            }
            return finishProbeFrame(frame);
        }

        [[nodiscard]] Result<VulkanFrameRecordResult> recordUnsupportedWireframeCheck(
            const VulkanFrameRecordContext& frame, BasicFullscreenTextureRenderer& renderer,
            BasicRenderViewDiagnostics& diagnostics, bool& unavailableReported) {
            const auto items = baseSceneItems();
            auto rejected = renderer.recordViewFrame(
                frame, BasicRenderViewDesc{
                           .target = swapchainViewTarget(frame),
                           .viewKind = BasicRenderViewKind::Scene,
                           .camera = sceneMeshCamera(),
                           .frameParams = BasicRenderViewFrameParams{.frameIndex = 80U},
                           .scene =
                               BasicRenderViewSceneDesc{
                                   .drawItems = items,
                                   .rasterMode = BasicSceneRasterMode::Wireframe,
                               },
                           .overlay = {},
                           .viewName = "RenderViewUnavailableWireframeSmoke",
                           .diagnostics = &diagnostics,
                       });
            if (rejected) {
                return std::unexpected{sceneMeshSmokeError(
                    "Scene mesh smoke silently rendered wireframe without fillModeNonSolid")};
            }
            unavailableReported =
                rejected.error().domain == ErrorDomain::Vulkan &&
                rejected.error().code == static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT) &&
                rejected.error().message.find("fillModeNonSolid") != std::string::npos &&
                rejected.error().message.find("no solid fallback was selected") !=
                    std::string::npos &&
                diagnostics.scene.wireframePath == BasicSceneWireframePath::Unavailable &&
                diagnostics.renderGraph.passes.empty() &&
                diagnostics.renderGraph.commands.empty() && diagnostics.executionEvents.empty();
            if (!unavailableReported) {
                return std::unexpected{sceneMeshSmokeError(
                    "Scene mesh smoke did not expose typed wireframe capability failure: " +
                    rejected.error().message)};
            }
            return finishProbeFrame(frame);
        }

        [[nodiscard]] std::optional<std::uint32_t>
        accessResourceIndex(const BasicRenderViewDiagnostics& diagnostics, std::size_t passIndex,
                            std::string_view slotName, RenderGraphResourceKind kind,
                            RenderGraphSlotAccess access) {
            const auto edge = std::ranges::find_if(
                diagnostics.renderGraph.accessEdges,
                [passIndex, slotName, kind, access](const RenderGraphDiagnosticsAccessEdge& item) {
                    return item.passIndex == passIndex && item.slotName == slotName &&
                           item.resourceKind == kind && item.access == access;
                });
            if (edge == diagnostics.renderGraph.accessEdges.end()) {
                return std::nullopt;
            }
            return edge->resourceIndex;
        }

        [[nodiscard]] constexpr bool packetContextMatches(BasicDrawPacketContext lhs,
                                                          BasicDrawPacketContext rhs) {
            return lhs.sourceObject == rhs.sourceObject && lhs.meshResource == rhs.meshResource &&
                   lhs.materialResource == rhs.materialResource;
        }

        [[nodiscard]] const RenderGraphDiagnosticsCommandNode*
        findPassCommand(const BasicRenderViewDiagnostics& diagnostics, std::size_t passIndex,
                        std::size_t commandIndex) {
            const auto command = std::ranges::find_if(
                diagnostics.renderGraph.commands,
                [passIndex, commandIndex](const RenderGraphDiagnosticsCommandNode& node) {
                    return node.passIndex == passIndex && node.commandIndex == commandIndex;
                });
            return command == diagnostics.renderGraph.commands.end() ? nullptr : &*command;
        }

        struct SceneOverlayExpectation {
            bool enabled{};
            bool worldGridEnabled{};
            std::uint64_t debugWorldLineCount{};
        };

        struct SceneDiagnosticsExpectation {
            std::span<const BasicDrawListItem> items;
            BasicSceneRasterMode rasterMode{BasicSceneRasterMode::Solid};
            BasicRenderViewKind viewKind{BasicRenderViewKind::Scene};
            std::string_view context;
            SceneOverlayExpectation overlay;
        };

        struct ScenePassResources {
            std::uint32_t target{};
            std::uint32_t depth{};
            std::uint32_t vertices{};
            std::uint32_t indices{};
        };

        struct ScenePassReceipt {
            std::size_t passIndex{};
            ScenePassResources resources;
            std::vector<const RenderGraphDiagnosticsCommandNode*> drawCommands;
            std::vector<const BasicRenderViewExecutionEvent*> drawEvents;
        };

        [[nodiscard]] bool
        validateSceneBatchReceipt(const SceneMeshProbe& probe,
                                  const SceneDiagnosticsExpectation& expectation) {
            const BasicRenderViewDiagnostics& diagnostics = probe.diagnostics;
            if (!probe.diagnosticsRecorded || diagnostics.viewKind != expectation.viewKind ||
                diagnostics.overlay.enabled != expectation.overlay.enabled ||
                diagnostics.overlay.worldGridEnabled != expectation.overlay.worldGridEnabled ||
                diagnostics.overlay.debugWorldLineCount !=
                    expectation.overlay.debugWorldLineCount) {
                logError(std::string{expectation.context} +
                         " did not preserve view kind or overlay policy");
                return false;
            }
            if (diagnostics.scene.drawItemCount != expectation.items.size() ||
                diagnostics.scene.indexedDrawCount != expectation.items.size() ||
                diagnostics.scene.rasterMode != expectation.rasterMode ||
                diagnostics.scene.meshResource != kBasicValidationMeshResourceKey ||
                diagnostics.scene.materialResource != kBasicDefaultUnlitMaterialResourceKey ||
                diagnostics.scene.drawPacketContexts.size() != expectation.items.size()) {
                logError(std::string{expectation.context} +
                         " recorded an invalid scene batch receipt");
                return false;
            }
            const BasicSceneWireframePath expectedWireframePath =
                expectation.rasterMode == BasicSceneRasterMode::Wireframe
                    ? BasicSceneWireframePath::PolygonLine
                    : BasicSceneWireframePath::NotRequested;
            if (diagnostics.scene.wireframePath != expectedWireframePath) {
                logError(std::string{expectation.context} +
                         " recorded an invalid wireframe path receipt");
                return false;
            }
            for (std::size_t index = 0; index < expectation.items.size(); ++index) {
                if (!packetContextMatches(diagnostics.scene.drawPacketContexts[index],
                                          expectation.items[index].context)) {
                    logError(std::string{expectation.context} +
                             " did not preserve immutable draw packet context order");
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::optional<ScenePassReceipt>
        resolveScenePassReceipt(const BasicRenderViewDiagnostics& diagnostics,
                                const SceneDiagnosticsExpectation& expectation) {
            const auto scenePass = std::ranges::find_if(
                diagnostics.renderGraph.passes, [](const RenderGraphDiagnosticsPassNode& pass) {
                    return pass.type == kBasicRenderViewSceneMeshPassType;
                });
            if (scenePass == diagnostics.renderGraph.passes.end() ||
                scenePass->commandCount != expectation.items.size() + 3U ||
                diagnostics.renderGraph.declaredBufferCount < 2U) {
                logError(std::string{expectation.context} +
                         " did not expose the Scene mesh RenderGraph pass or buffer resources");
                return std::nullopt;
            }

            const std::optional<std::uint32_t> targetResource = accessResourceIndex(
                diagnostics, scenePass->passIndex, "target", RenderGraphResourceKind::Image,
                RenderGraphSlotAccess::ColorReadWrite);
            const std::optional<std::uint32_t> depthResource = accessResourceIndex(
                diagnostics, scenePass->passIndex, "depth", RenderGraphResourceKind::Image,
                RenderGraphSlotAccess::DepthAttachmentWrite);
            const std::optional<std::uint32_t> vertexResource = accessResourceIndex(
                diagnostics, scenePass->passIndex, "vertices", RenderGraphResourceKind::Buffer,
                RenderGraphSlotAccess::BufferVertexRead);
            const std::optional<std::uint32_t> indexResource = accessResourceIndex(
                diagnostics, scenePass->passIndex, "indices", RenderGraphResourceKind::Buffer,
                RenderGraphSlotAccess::BufferIndexRead);
            if (!targetResource || !depthResource || !vertexResource || !indexResource) {
                logError(std::string{expectation.context} +
                         " did not bind color/depth/vertex/index RenderGraph slots");
                return std::nullopt;
            }

            ScenePassReceipt receipt{
                .passIndex = scenePass->passIndex,
                .resources =
                    ScenePassResources{
                        .target = *targetResource,
                        .depth = *depthResource,
                        .vertices = *vertexResource,
                        .indices = *indexResource,
                    },
                .drawCommands = {},
                .drawEvents = {},
            };
            for (const RenderGraphDiagnosticsCommandNode& command :
                 diagnostics.renderGraph.commands) {
                if (command.passIndex == receipt.passIndex &&
                    command.kind == RenderGraphCommandKind::DrawIndexed) {
                    receipt.drawCommands.push_back(&command);
                }
            }
            for (const BasicRenderViewExecutionEvent& event : diagnostics.executionEvents) {
                if (event.passIndex == receipt.passIndex &&
                    event.kind == BasicRenderViewExecutionEventKind::DrawIndexed) {
                    receipt.drawEvents.push_back(&event);
                }
            }
            if (receipt.drawCommands.size() != expectation.items.size() ||
                receipt.drawEvents.size() != expectation.items.size()) {
                logError(std::string{expectation.context} +
                         " did not expose one DrawIndexed command and event per draw item");
                return std::nullopt;
            }
            return receipt;
        }

        [[nodiscard]] bool executionEventMatches(const BasicRenderViewExecutionEvent& event,
                                                 const BasicDrawListItem& expected,
                                                 std::size_t itemIndex,
                                                 const ScenePassResources& resources) {
            return event.commandIndex && event.sceneDrawItemIndex &&
                   *event.sceneDrawItemIndex == itemIndex && event.drawPacketContext &&
                   packetContextMatches(*event.drawPacketContext, expected.context) &&
                   event.targetImageResourceIndex == resources.target &&
                   event.depthImageResourceIndex == resources.depth &&
                   event.vertexBufferResourceIndex == resources.vertices &&
                   event.indexBufferResourceIndex == resources.indices &&
                   event.draw.indexCount == expected.drawItem.indexCount &&
                   event.draw.instanceCount == expected.drawItem.instanceCount &&
                   event.draw.firstIndex == expected.drawItem.firstIndex &&
                   event.draw.vertexOffset == expected.drawItem.vertexOffset &&
                   event.draw.firstInstance == expected.drawItem.firstInstance;
        }

        [[nodiscard]] std::string expectedDrawCommandDetail(const BasicDrawListItem& expected) {
            return "indexCount=" + std::to_string(expected.drawItem.indexCount) +
                   ", instanceCount=" + std::to_string(expected.drawItem.instanceCount) +
                   ", firstIndex=" + std::to_string(expected.drawItem.firstIndex) +
                   ", vertexOffset=" + std::to_string(expected.drawItem.vertexOffset) +
                   ", firstInstance=" + std::to_string(expected.drawItem.firstInstance);
        }

        [[nodiscard]] bool validateDrawReceipt(const BasicRenderViewDiagnostics& diagnostics,
                                               const ScenePassReceipt& receipt,
                                               const BasicDrawListItem& expected,
                                               std::size_t itemIndex, std::string_view context) {
            const BasicRenderViewExecutionEvent& event = *receipt.drawEvents.at(itemIndex);
            if (!executionEventMatches(event, expected, itemIndex, receipt.resources)) {
                logError(std::string{context} +
                         " lost DrawIndexed execution/resource/packet context");
                return false;
            }
            const RenderGraphDiagnosticsCommandNode* linkedCommand =
                findPassCommand(diagnostics, receipt.passIndex, *event.commandIndex);
            if (linkedCommand == nullptr ||
                linkedCommand->kind != RenderGraphCommandKind::DrawIndexed ||
                linkedCommand != receipt.drawCommands.at(itemIndex)) {
                logError(std::string{context} +
                         " did not link DrawIndexed execution to its command summary");
                return false;
            }
            if (linkedCommand->detail != expectedDrawCommandDetail(expected)) {
                logError(std::string{context} +
                         " recorded incorrect DrawIndexed command arguments");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateDrawReceipts(const BasicRenderViewDiagnostics& diagnostics,
                                                const ScenePassReceipt& receipt,
                                                const SceneDiagnosticsExpectation& expectation) {
            for (std::size_t index = 0; index < expectation.items.size(); ++index) {
                if (!validateDrawReceipt(diagnostics, receipt, expectation.items[index], index,
                                         expectation.context)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool validateSceneDiagnostics(
            const SceneMeshProbe& probe, std::span<const BasicDrawListItem> expectedItems,
            BasicSceneRasterMode expectedRasterMode, BasicRenderViewKind expectedViewKind,
            std::string_view context, SceneOverlayExpectation overlay = {}) {
            const SceneDiagnosticsExpectation expectation{
                .items = expectedItems,
                .rasterMode = expectedRasterMode,
                .viewKind = expectedViewKind,
                .context = context,
                .overlay = overlay,
            };
            if (!validateSceneBatchReceipt(probe, expectation)) {
                return false;
            }
            const std::optional<ScenePassReceipt> receipt =
                resolveScenePassReceipt(probe.diagnostics, expectation);
            return receipt && validateDrawReceipts(probe.diagnostics, *receipt, expectation);
        }

        [[nodiscard]] bool validateEmptyDiagnostics(const SceneMeshProbe& probe) {
            if (!probe.diagnosticsRecorded || probe.diagnostics.scene.drawItemCount != 0U ||
                probe.diagnostics.scene.indexedDrawCount != 0U ||
                probe.diagnostics.scene.meshResource || probe.diagnostics.scene.materialResource ||
                probe.diagnostics.overlay.enabled) {
                logError("Scene mesh empty probe recorded non-empty scene diagnostics");
                return false;
            }
            const bool hasScenePass =
                std::ranges::any_of(probe.diagnostics.renderGraph.passes,
                                    [](const RenderGraphDiagnosticsPassNode& pass) {
                                        return pass.type == kBasicRenderViewSceneMeshPassType;
                                    });
            if (hasScenePass) {
                logError("Scene mesh empty probe recorded a Scene mesh pass");
                return false;
            }
            return true;
        }

        [[nodiscard]] std::uint32_t pixelDelta(std::span<const std::byte> lhs,
                                               std::span<const std::byte> rhs,
                                               std::size_t pixelIndex) {
            const std::size_t byteIndex = pixelIndex * static_cast<std::size_t>(kBytesPerPixel);
            std::uint32_t delta{};
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const int left = std::to_integer<unsigned char>(lhs[byteIndex + channel]);
                const int right = std::to_integer<unsigned char>(rhs[byteIndex + channel]);
                delta += static_cast<std::uint32_t>(std::abs(left - right));
            }
            return delta;
        }

        [[nodiscard]] std::vector<std::uint8_t> changedMask(std::span<const std::byte> pixels,
                                                            std::span<const std::byte> baseline) {
            const std::size_t pixelCount =
                std::min(pixels.size(), baseline.size()) / static_cast<std::size_t>(kBytesPerPixel);
            std::vector<std::uint8_t> mask(pixelCount);
            for (std::size_t index = 0; index < pixelCount; ++index) {
                mask[index] =
                    pixelDelta(pixels, baseline, index) > kChangedPixelThreshold ? 1U : 0U;
            }
            return mask;
        }

        [[nodiscard]] std::uint64_t countMask(std::span<const std::uint8_t> mask,
                                              HorizontalRegion region) {
            std::uint64_t count{};
            for (std::uint32_t row = 0; row < kReadbackExtent.height; ++row) {
                for (std::uint32_t column = region.firstCoordinate;
                     column < region.pastLastCoordinate; ++column) {
                    count += mask[(static_cast<std::size_t>(row) * kReadbackExtent.width) + column];
                }
            }
            return count;
        }

        [[nodiscard]] std::uint64_t imageDifference(std::span<const std::byte> lhs,
                                                    std::span<const std::byte> rhs,
                                                    HorizontalRegion region) {
            std::uint64_t difference{};
            for (std::uint32_t row = 0; row < kReadbackExtent.height; ++row) {
                for (std::uint32_t column = region.firstCoordinate;
                     column < region.pastLastCoordinate; ++column) {
                    const std::size_t index =
                        (static_cast<std::size_t>(row) * kReadbackExtent.width) + column;
                    difference += pixelDelta(lhs, rhs, index);
                }
            }
            return difference;
        }

        [[nodiscard]] std::uint32_t maximumUniformDelta(std::span<const std::byte> pixels) {
            if (pixels.size() < static_cast<std::size_t>(kBytesPerPixel)) {
                return std::numeric_limits<std::uint32_t>::max();
            }
            std::uint32_t maximum{};
            const std::size_t pixelCount = pixels.size() / static_cast<std::size_t>(kBytesPerPixel);
            for (std::size_t index = 1; index < pixelCount; ++index) {
                std::uint32_t delta{};
                const std::size_t byteIndex = index * static_cast<std::size_t>(kBytesPerPixel);
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    const int value = std::to_integer<unsigned char>(pixels[byteIndex + channel]);
                    const int reference = std::to_integer<unsigned char>(pixels[channel]);
                    delta += static_cast<std::uint32_t>(std::abs(value - reference));
                }
                maximum = std::max(maximum, delta);
            }
            return maximum;
        }

        [[nodiscard]] std::pair<std::uint64_t, std::uint64_t>
        interiorClearMetrics(MaskComparison masks) {
            constexpr std::uint32_t kRadius = 2U;
            std::uint64_t interior{};
            std::uint64_t cleared{};
            auto changed = [&masks](std::uint32_t column, std::uint32_t row) {
                return masks.solid[(static_cast<std::size_t>(row) * kReadbackExtent.width) +
                                   column] != 0U;
            };
            for (std::uint32_t row = kRadius; row + kRadius < kReadbackExtent.height; ++row) {
                for (std::uint32_t column = kRadius; column + kRadius < kReadbackExtent.width;
                     ++column) {
                    if (!changed(column, row) || !changed(column - kRadius, row) ||
                        !changed(column + kRadius, row) || !changed(column, row - kRadius) ||
                        !changed(column, row + kRadius)) {
                        continue;
                    }
                    ++interior;
                    const std::size_t index =
                        (static_cast<std::size_t>(row) * kReadbackExtent.width) + column;
                    if (masks.wireframe[index] == 0U) {
                        ++cleared;
                    }
                }
            }
            return {interior, cleared};
        }

        [[nodiscard]] bool readProbePixels(std::span<SceneMeshProbe> probes) {
            for (SceneMeshProbe& probe : probes) {
                auto read = probe.readback.read(std::span<std::byte>{probe.pixels});
                if (!read) {
                    logError(read.error().message);
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool validatePixels(std::span<const SceneMeshProbe, kProbeCount> probes,
                                          bool wireframeAvailable, SmokePixelMetrics& metrics) {
            constexpr HorizontalRegion kFullRegion{
                .firstCoordinate = 0U,
                .pastLastCoordinate = kReadbackExtent.width,
            };
            constexpr HorizontalRegion kLeftRegion{
                .firstCoordinate = 0U,
                .pastLastCoordinate = kReadbackExtent.width / 2U,
            };
            constexpr HorizontalRegion kRightRegion{
                .firstCoordinate = kReadbackExtent.width / 2U,
                .pastLastCoordinate = kReadbackExtent.width,
            };
            const auto& empty = probes[probeIndex(ProbeKind::Empty)].pixels;
            const auto& solid = probes[probeIndex(ProbeKind::Solid)].pixels;
            const auto& moved = probes[probeIndex(ProbeKind::Moved)].pixels;
            const auto& depthA = probes[probeIndex(ProbeKind::DepthNearThenFar)].pixels;
            const auto& depthB = probes[probeIndex(ProbeKind::DepthFarThenNear)].pixels;
            if (maximumUniformDelta(empty) != 0U) {
                logError("Scene mesh empty probe was not a uniform clear frame");
                return false;
            }

            const std::vector solidMask = changedMask(solid, empty);
            metrics.solidPixels = countMask(solidMask, kFullRegion);
            metrics.solidLeftPixels = countMask(solidMask, kLeftRegion);
            metrics.solidRightPixels = countMask(solidMask, kRightRegion);
            if (metrics.solidPixels < kMinimumSolidPixels ||
                metrics.solidLeftPixels < kMinimumSolidPixelsPerHalf ||
                metrics.solidRightPixels < kMinimumSolidPixelsPerHalf) {
                logError("Scene mesh solid probe did not show both transformed mesh items");
                return false;
            }

            metrics.movedLeftDifference = imageDifference(solid, moved, kLeftRegion);
            metrics.movedRightDifference = imageDifference(solid, moved, kRightRegion);
            if (metrics.movedLeftDifference > kMaximumStationaryLeftDifference ||
                metrics.movedRightDifference < kMinimumMovedRightDifference) {
                logError("Scene mesh transform probe did not isolate the moved right draw item");
                return false;
            }

            const std::vector depthMask = changedMask(depthA, empty);
            if (countMask(depthMask, kFullRegion) < kMinimumSolidPixelsPerHalf) {
                logError("Scene mesh depth probe did not render overlapping geometry");
                return false;
            }
            metrics.depthOrderDifference = imageDifference(depthA, depthB, kFullRegion);
            if (metrics.depthOrderDifference > kMaximumOrderDifference) {
                logError("Scene mesh depth result changed when near/far draw order was reversed");
                return false;
            }

            if (!wireframeAvailable) {
                return true;
            }
            const auto& wire = probes[probeIndex(ProbeKind::Wireframe)].pixels;
            const std::vector wireMask = changedMask(wire, empty);
            metrics.wirePixels = countMask(wireMask, kFullRegion);
            const auto [interior, cleared] = interiorClearMetrics(MaskComparison{
                .solid = solidMask,
                .wireframe = wireMask,
            });
            metrics.solidInteriorPixels = interior;
            metrics.clearedSolidInteriorPixels = cleared;
            if (metrics.wirePixels < kMinimumWirePixels ||
                metrics.wirePixels * 2U >= metrics.solidPixels ||
                metrics.solidInteriorPixels < kMinimumSolidInteriorPixels ||
                metrics.clearedSolidInteriorPixels * 100U < metrics.solidInteriorPixels * 70U) {
                logError("Scene mesh wireframe probe did not preserve edges while clearing solid "
                         "interiors");
                return false;
            }

            metrics.sameFrameWireDifference = imageDifference(
                wire, probes[probeIndex(ProbeKind::SameFrameSceneWireframe)].pixels, kFullRegion);
            metrics.sameFrameSolidDifference = imageDifference(
                solid, probes[probeIndex(ProbeKind::SameFrameGameSolid)].pixels, kFullRegion);
            if (metrics.sameFrameWireDifference > kMaximumSameFrameDifference ||
                metrics.sameFrameSolidDifference > kMaximumSameFrameDifference) {
                logError("Scene/Game views leaked raster modes within one submitted frame");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSceneMeshBufferStats(VulkanBufferStats stats) {
            if (stats.created != 3U || stats.hostUploadCreated != 3U || stats.uploadCalls != 3U ||
                stats.allocatedBytes == 0U || stats.uploadedBytes == 0U ||
                stats.uploadedBytes > stats.allocatedBytes) {
                logError("Scene mesh smoke did not lazily create exactly one uniform, vertex, and "
                         "index buffer");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSceneMeshPipelineStats(BasicPipelineCacheStats stats,
                                                          bool wireframeAvailable) {
            const std::uint64_t expectedCreated = wireframeAvailable ? 2U : 1U;
            const std::uint64_t expectedReused = wireframeAvailable ? 5U : 3U;
            if (stats.created != expectedCreated || stats.reused != expectedReused) {
                logError("Scene mesh smoke did not independently create and reuse Solid/Wireframe "
                         "pipelines");
                return false;
            }
            return true;
        }

        struct SceneMeshSmokeRunState {
            std::reference_wrapper<VulkanContext> context;
            std::reference_wrapper<GlfwWindow> window;
            std::reference_wrapper<VulkanFrameLoop> frameLoop;
            std::reference_wrapper<BasicFullscreenTextureRenderer> renderer;
            std::reference_wrapper<std::array<SceneMeshProbe, kProbeCount>> probes;
            std::uint64_t frameIndex{1U};
            bool wireframeAvailable{};
            bool wireframeUnavailableReported{};
            UnknownResourceCheckState unknownResources;
            BasicPipelineCacheStats retainedFormatScenePipelineStats;
            BasicPipelineCacheStats retainedFormatWorldGridPipelineStats;
            BasicPipelineCacheStats retainedFormatDebugLinePipelineStats;
            bool retainedFormatCacheVerified{};
        };

        [[nodiscard]] bool createSceneMeshProbes(VulkanContext& context,
                                                 std::array<SceneMeshProbe, kProbeCount>& probes) {
            for (SceneMeshProbe& probe : probes) {
                auto created = createProbe(context);
                if (!created) {
                    logError(created.error().message);
                    return false;
                }
                probe = std::move(*created);
            }
            return true;
        }

        [[nodiscard]] bool submitSceneMeshProbe(SceneMeshSmokeRunState& state, ProbeKind kind,
                                                std::span<const BasicDrawListItem> items,
                                                BasicSceneRasterMode rasterMode,
                                                BasicRenderViewKind viewKind,
                                                std::string_view viewName) {
            SceneMeshProbe& probe = state.probes.get().at(probeIndex(kind));
            const std::uint64_t currentFrameIndex = state.frameIndex++;
            return submitSmokeFrame(
                state.frameLoop.get(), state.window.get(),
                [&state, &probe, items, rasterMode, viewKind, currentFrameIndex, viewName](
                    const VulkanFrameRecordContext& frame) -> Result<VulkanFrameRecordResult> {
                    auto recorded =
                        recordProbeView(frame, state.context.get(), state.renderer.get(), probe,
                                        items, rasterMode, viewKind, currentFrameIndex, viewName);
                    if (!recorded) {
                        return std::unexpected{std::move(recorded.error())};
                    }
                    return finishProbeFrame(frame);
                },
                viewName);
        }

        [[nodiscard]] bool recordRequiredSceneMeshProbes(SceneMeshSmokeRunState& state) {
            constexpr std::array<BasicDrawListItem, 0> kEmptyItems{};
            const std::array<BasicDrawListItem, 2> baseItems = baseSceneItems();
            const std::array<BasicDrawListItem, 2> movedItems = movedSceneItems();
            const std::array<BasicDrawListItem, 2> depthNearFar = depthSceneItems(false);
            const std::array<BasicDrawListItem, 2> depthFarNear = depthSceneItems(true);

            if (!submitSceneMeshProbe(state, ProbeKind::Empty, kEmptyItems,
                                      BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                      "RenderViewSceneMeshEmpty")) {
                return false;
            }
            if (!submitSceneMeshProbe(state, ProbeKind::Solid, baseItems,
                                      BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                      "RenderViewSceneMeshSolid")) {
                return false;
            }
            if (!submitSceneMeshProbe(state, ProbeKind::Moved, movedItems,
                                      BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                      "RenderViewSceneMeshMoved")) {
                return false;
            }
            if (!submitSceneMeshProbe(state, ProbeKind::DepthNearThenFar, depthNearFar,
                                      BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                      "RenderViewSceneMeshDepthNearFar")) {
                return false;
            }
            return submitSceneMeshProbe(state, ProbeKind::DepthFarThenNear, depthFarNear,
                                        BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                        "RenderViewSceneMeshDepthFarNear");
        }

        [[nodiscard]] bool recordSameFrameSceneMeshViews(SceneMeshSmokeRunState& state,
                                                         std::span<const BasicDrawListItem> items) {
            SceneMeshProbe& sceneWire =
                state.probes.get().at(probeIndex(ProbeKind::SameFrameSceneWireframe));
            SceneMeshProbe& gameSolid =
                state.probes.get().at(probeIndex(ProbeKind::SameFrameGameSolid));
            const std::uint64_t sameFrameIndex = state.frameIndex++;
            return submitSmokeFrame(
                state.frameLoop.get(), state.window.get(),
                [&state, &sceneWire, &gameSolid, items, sameFrameIndex](
                    const VulkanFrameRecordContext& frame) -> Result<VulkanFrameRecordResult> {
                    auto wireRecorded = recordProbeView(
                        frame, state.context.get(), state.renderer.get(), sceneWire, items,
                        BasicSceneRasterMode::Wireframe, BasicRenderViewKind::Scene, sameFrameIndex,
                        "RenderViewSameFrameSceneWireframe");
                    if (!wireRecorded) {
                        return std::unexpected{std::move(wireRecorded.error())};
                    }
                    auto solidRecorded = recordProbeView(
                        frame, state.context.get(), state.renderer.get(), gameSolid, items,
                        BasicSceneRasterMode::Solid, BasicRenderViewKind::Game, sameFrameIndex,
                        "RenderViewSameFrameGameSolid");
                    if (!solidRecorded) {
                        return std::unexpected{std::move(solidRecorded.error())};
                    }
                    return finishProbeFrame(frame);
                },
                "RenderView same-frame Scene/Game raster policy");
        }

        [[nodiscard]] bool recordWireframeSceneMeshProbes(SceneMeshSmokeRunState& state) {
            if (!state.wireframeAvailable) {
                BasicRenderViewDiagnostics diagnostics;
                return submitSmokeFrame(
                    state.frameLoop.get(), state.window.get(),
                    [&state, &diagnostics](const VulkanFrameRecordContext& frame) {
                        return recordUnsupportedWireframeCheck(frame, state.renderer.get(),
                                                               diagnostics,
                                                               state.wireframeUnavailableReported);
                    },
                    "RenderView unavailable wireframe capability");
            }

            const std::array<BasicDrawListItem, 2> baseItems = baseSceneItems();
            if (!submitSceneMeshProbe(state, ProbeKind::Wireframe, baseItems,
                                      BasicSceneRasterMode::Wireframe, BasicRenderViewKind::Scene,
                                      "RenderViewSceneMeshWireframe")) {
                return false;
            }
            return recordSameFrameSceneMeshViews(state, baseItems);
        }

        [[nodiscard]] bool
        recordDeterministicUnavailableWireframeCheck(SceneMeshSmokeRunState& state) {
            auto createdRenderer =
                BasicFullscreenTextureRenderer::create(BasicFullscreenTextureRendererDesc{
                    .device = state.context.get().device(),
                    .allocator = state.context.get().allocator(),
                    .shaderDirectory =
                        std::filesystem::path{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR},
                    .deviceCapabilities = {},
                });
            if (!createdRenderer) {
                logError(createdRenderer.error().message);
                return false;
            }
            BasicFullscreenTextureRenderer unavailableRenderer = std::move(*createdRenderer);
            const BasicPipelineCacheStats statsBefore =
                unavailableRenderer.sceneMeshPipelineCacheStats();
            BasicRenderViewDiagnostics diagnostics;
            bool unavailableReported{};
            if (!submitSmokeFrame(
                    state.frameLoop.get(), state.window.get(),
                    [&unavailableRenderer, &diagnostics,
                     &unavailableReported](const VulkanFrameRecordContext& frame) {
                        return recordUnsupportedWireframeCheck(frame, unavailableRenderer,
                                                               diagnostics, unavailableReported);
                    },
                    "RenderView deterministic unavailable wireframe capability")) {
                return false;
            }
            const BasicPipelineCacheStats statsAfter =
                unavailableRenderer.sceneMeshPipelineCacheStats();
            if (!unavailableReported || statsBefore.created != 0U || statsBefore.reused != 0U ||
                statsAfter.created != 0U || statsAfter.reused != 0U) {
                logError("Scene mesh smoke selected a Solid pipeline fallback for unavailable "
                         "Wireframe");
                return false;
            }
            return true;
        }

        [[nodiscard]] Result<VulkanFrameRecordResult> recordRetainedFormatSceneMeshViews(
            const VulkanFrameRecordContext& frame, SceneMeshSmokeRunState& state,
            BasicFullscreenTextureRenderer& renderer,
            std::array<SceneMeshProbe, kRetainedFormatProbeCount>& probes,
            std::span<const BasicDrawListItem> items, std::uint64_t frameIndex) {
            constexpr std::array<std::string_view, kRetainedFormatProbeCount> kViewNames{
                "RenderViewRetainedFormatSrgbFirst",
                "RenderViewRetainedFormatUnorm",
                "RenderViewRetainedFormatSrgbReused",
            };
            constexpr std::array<BasicRenderViewOverlayBlendMode, kRetainedFormatProbeCount>
                kBlendModes{
                    BasicRenderViewOverlayBlendMode::AlphaBlend,
                    BasicRenderViewOverlayBlendMode::Additive,
                    BasicRenderViewOverlayBlendMode::AlphaBlend,
                };
            constexpr std::array debugLines{
                BasicDebugWorldLine{
                    .start = {-0.8F, 0.0F, 3.0F},
                    .end = {0.8F, 0.0F, 3.0F},
                    .color = {0.95F, 0.25F, 0.15F, 0.8F},
                },
            };
            for (std::size_t index = 0; index < probes.size(); ++index) {
                const BasicRenderViewOverlayDesc overlay{
                    .enabled = true,
                    .colorLoadOp = BasicRenderViewOverlayColorLoadOp::LoadSceneColor,
                    .colorStoreOp = BasicRenderViewOverlayColorStoreOp::Store,
                    .blendMode = kBlendModes.at(index),
                    .worldGrid =
                        BasicRenderViewWorldGridDesc{
                            .enabled = true,
                            .planeY = 0.0F,
                            .minorSpacing = 1.0F,
                            .majorSpacing = 10.0F,
                            .fadeStart = 0.0F,
                            .fadeEnd = 0.0F,
                            .opacity = 0.65F,
                            .color = {0.36F, 0.39F, 0.44F, 1.0F},
                        },
                    .sourceOverlayIds = {},
                    .debugWorldLines = debugLines,
                };
                auto recorded =
                    recordProbeView(frame, state.context.get(), renderer, probes.at(index), items,
                                    BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                                    frameIndex, kViewNames.at(index), overlay);
                if (!recorded) {
                    return std::unexpected{std::move(recorded.error())};
                }
            }
            return finishProbeFrame(frame);
        }

        [[nodiscard]] bool validateRetainedFormatSceneMeshDiagnostics(
            std::span<const SceneMeshProbe, kRetainedFormatProbeCount> probes,
            std::span<const BasicDrawListItem> items) {
            constexpr std::array<std::string_view, kRetainedFormatProbeCount> kContexts{
                "Retained format SRGB first probe",
                "Retained format UNORM probe",
                "Retained format SRGB reuse probe",
            };
            for (std::size_t index = 0; index < probes.size(); ++index) {
                if (!validateSceneDiagnostics(probes[index], items, BasicSceneRasterMode::Solid,
                                              BasicRenderViewKind::Scene, kContexts.at(index),
                                              SceneOverlayExpectation{
                                                  .enabled = true,
                                                  .worldGridEnabled = true,
                                                  .debugWorldLineCount = 1U,
                                              })) {
                    return false;
                }
                const bool hasWorldGridPass =
                    std::ranges::any_of(probes[index].diagnostics.renderGraph.passes,
                                        [](const RenderGraphDiagnosticsPassNode& pass) {
                                            return pass.type == kBasicRenderViewWorldGridPassType;
                                        });
                const bool hasDebugLinePass =
                    std::ranges::any_of(probes[index].diagnostics.renderGraph.passes,
                                        [](const RenderGraphDiagnosticsPassNode& pass) {
                                            return pass.type == kBasicRenderViewOverlayPassType;
                                        });
                if (!hasWorldGridPass || !hasDebugLinePass) {
                    logError(std::string{kContexts.at(index)} +
                             " did not record both retained overlay passes");
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool recordRetainedFormatSceneMeshCheck(SceneMeshSmokeRunState& state) {
            auto createdRenderer =
                BasicFullscreenTextureRenderer::create(BasicFullscreenTextureRendererDesc{
                    .device = state.context.get().device(),
                    .allocator = state.context.get().allocator(),
                    .shaderDirectory =
                        std::filesystem::path{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR},
                    .deviceCapabilities = state.context.get().capabilities(),
                });
            if (!createdRenderer) {
                logError(createdRenderer.error().message);
                return false;
            }
            BasicFullscreenTextureRenderer formatRenderer = std::move(*createdRenderer);
            std::array<SceneMeshProbe, kRetainedFormatProbeCount> formatProbes;
            for (SceneMeshProbe& probe : formatProbes) {
                auto created = createProbe(state.context.get());
                if (!created) {
                    logError(created.error().message);
                    return false;
                }
                probe = std::move(*created);
            }
            formatProbes.at(1).targetFormat = VK_FORMAT_B8G8R8A8_UNORM;

            const std::array<BasicDrawListItem, 2> items = baseSceneItems();
            const std::uint64_t frameIndex = state.frameIndex++;
            if (!submitSmokeFrame(
                    state.frameLoop.get(), state.window.get(),
                    [&state, &formatRenderer, &formatProbes, &items,
                     frameIndex](const VulkanFrameRecordContext& frame) {
                        return recordRetainedFormatSceneMeshViews(frame, state, formatRenderer,
                                                                  formatProbes, items, frameIndex);
                    },
                    "RenderView retained different-format Scene pipelines")) {
                return false;
            }
            const VkResult idleResult = vkQueueWaitIdle(state.context.get().graphicsQueue());
            if (idleResult != VK_SUCCESS) {
                logError("Failed to wait for retained Scene mesh pipelines: " +
                         vkResultName(idleResult));
                return false;
            }
            if (!validateRetainedFormatSceneMeshDiagnostics(formatProbes, items)) {
                return false;
            }

            const BasicPipelineCacheStats sceneStats = formatRenderer.sceneMeshPipelineCacheStats();
            const BasicPipelineCacheStats fullscreenStats = formatRenderer.pipelineCacheStats();
            const BasicPipelineCacheStats worldGridStats =
                formatRenderer.worldGridPipelineCacheStats();
            const BasicPipelineCacheStats debugLineStats =
                formatRenderer.debugLinePipelineCacheStats();
            if (sceneStats.created != 2U || sceneStats.reused != 1U ||
                fullscreenStats.created != 2U || fullscreenStats.reused != 1U ||
                worldGridStats.created != 2U || worldGridStats.reused != 1U ||
                debugLineStats.created != 2U || debugLineStats.reused != 1U) {
                logError("Retained RenderView pipeline caches did not report A/B/A as two creates "
                         "and one reuse per pass family");
                return false;
            }
            state.retainedFormatScenePipelineStats = sceneStats;
            state.retainedFormatWorldGridPipelineStats = worldGridStats;
            state.retainedFormatDebugLinePipelineStats = debugLineStats;
            state.retainedFormatCacheVerified = true;
            return true;
        }

        [[nodiscard]] bool recordUnknownSceneMeshResourceChecks(SceneMeshSmokeRunState& state) {
            return submitSmokeFrame(
                state.frameLoop.get(), state.window.get(),
                [&state](const VulkanFrameRecordContext& frame) {
                    return recordUnknownResourceChecks(frame, state.renderer.get(),
                                                       state.unknownResources);
                },
                "RenderView unresolved resource checks");
        }

        [[nodiscard]] bool recordAllSceneMeshProbes(SceneMeshSmokeRunState& state) {
            if (!recordRequiredSceneMeshProbes(state)) {
                return false;
            }
            if (!recordWireframeSceneMeshProbes(state)) {
                return false;
            }
            if (!recordDeterministicUnavailableWireframeCheck(state)) {
                return false;
            }
            if (!recordRetainedFormatSceneMeshCheck(state)) {
                return false;
            }
            return recordUnknownSceneMeshResourceChecks(state);
        }

        [[nodiscard]] bool waitForSceneMeshReadback(SceneMeshSmokeRunState& state) {
            const VkResult idleResult = vkQueueWaitIdle(state.context.get().graphicsQueue());
            if (idleResult != VK_SUCCESS) {
                logError("Failed to wait for Vulkan queue before Scene mesh readback: " +
                         vkResultName(idleResult));
                return false;
            }
            return readProbePixels(state.probes.get());
        }

        [[nodiscard]] bool
        validateRequiredSceneMeshDiagnostics(const SceneMeshSmokeRunState& state) {
            const std::array<BasicDrawListItem, 2> baseItems = baseSceneItems();
            const std::array<BasicDrawListItem, 2> movedItems = movedSceneItems();
            const std::array<BasicDrawListItem, 2> depthNearFar = depthSceneItems(false);
            const std::array<BasicDrawListItem, 2> depthFarNear = depthSceneItems(true);

            if (!validateEmptyDiagnostics(state.probes.get().at(probeIndex(ProbeKind::Empty)))) {
                return false;
            }
            if (!validateSceneDiagnostics(state.probes.get().at(probeIndex(ProbeKind::Solid)),
                                          baseItems, BasicSceneRasterMode::Solid,
                                          BasicRenderViewKind::Scene, "Scene mesh Solid probe")) {
                return false;
            }
            if (!validateSceneDiagnostics(state.probes.get().at(probeIndex(ProbeKind::Moved)),
                                          movedItems, BasicSceneRasterMode::Solid,
                                          BasicRenderViewKind::Scene, "Scene mesh moved probe")) {
                return false;
            }
            if (!validateSceneDiagnostics(
                    state.probes.get().at(probeIndex(ProbeKind::DepthNearThenFar)), depthNearFar,
                    BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                    "Scene mesh near/far depth probe")) {
                return false;
            }
            return validateSceneDiagnostics(
                state.probes.get().at(probeIndex(ProbeKind::DepthFarThenNear)), depthFarNear,
                BasicSceneRasterMode::Solid, BasicRenderViewKind::Scene,
                "Scene mesh reversed depth probe");
        }

        [[nodiscard]] bool
        validateWireframeSceneMeshDiagnostics(const SceneMeshSmokeRunState& state) {
            if (!state.wireframeAvailable) {
                return true;
            }
            const std::array<BasicDrawListItem, 2> baseItems = baseSceneItems();
            if (!validateSceneDiagnostics(state.probes.get().at(probeIndex(ProbeKind::Wireframe)),
                                          baseItems, BasicSceneRasterMode::Wireframe,
                                          BasicRenderViewKind::Scene,
                                          "Scene mesh Wireframe probe")) {
                return false;
            }
            if (!validateSceneDiagnostics(
                    state.probes.get().at(probeIndex(ProbeKind::SameFrameSceneWireframe)),
                    baseItems, BasicSceneRasterMode::Wireframe, BasicRenderViewKind::Scene,
                    "Same-frame Scene Wireframe probe")) {
                return false;
            }
            return validateSceneDiagnostics(
                state.probes.get().at(probeIndex(ProbeKind::SameFrameGameSolid)), baseItems,
                BasicSceneRasterMode::Solid, BasicRenderViewKind::Game,
                "Same-frame Game Solid probe");
        }

        [[nodiscard]] bool validateSceneMeshFailureReceipts(const SceneMeshSmokeRunState& state) {
            if (!state.wireframeAvailable && !state.wireframeUnavailableReported) {
                logError("Scene mesh smoke did not report unavailable wireframe capability");
                return false;
            }
            if (!state.unknownResources.meshRejected || !state.unknownResources.materialRejected) {
                logError("Scene mesh smoke did not fail closed for unknown resource keys");
                return false;
            }
            if (!state.retainedFormatCacheVerified ||
                state.retainedFormatScenePipelineStats.created != 2U ||
                state.retainedFormatScenePipelineStats.reused != 1U ||
                state.retainedFormatWorldGridPipelineStats.created != 2U ||
                state.retainedFormatWorldGridPipelineStats.reused != 1U ||
                state.retainedFormatDebugLinePipelineStats.created != 2U ||
                state.retainedFormatDebugLinePipelineStats.reused != 1U) {
                logError("Scene mesh smoke did not preserve its same-frame A/B/A format cache");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSceneMeshEvidence(const SceneMeshSmokeRunState& state,
                                                     SmokePixelMetrics& metrics) {
            if (!validateRequiredSceneMeshDiagnostics(state) ||
                !validateWireframeSceneMeshDiagnostics(state) ||
                !validateSceneMeshFailureReceipts(state)) {
                return false;
            }
            if (!validatePixels(std::span<const SceneMeshProbe, kProbeCount>{state.probes.get()},
                                state.wireframeAvailable, metrics) ||
                !validateSceneMeshBufferStats(state.renderer.get().bufferStats()) ||
                !validateSceneMeshPipelineStats(state.renderer.get().sceneMeshPipelineCacheStats(),
                                                state.wireframeAvailable)) {
                return false;
            }
            const VulkanDebugLabelStats labelStats = state.frameLoop.get().debugLabelStats();
            if (!labelStats.available || labelStats.regionsBegun == 0U ||
                labelStats.regionsBegun != labelStats.regionsEnded) {
                logError("Scene mesh smoke did not record balanced Vulkan debug labels");
                return false;
            }
            return true;
        }

        void printSceneMeshMetrics(const SmokePixelMetrics& metrics,
                                   const SceneMeshSmokeRunState& state) {
            std::cout << "RenderView Scene mesh readback: " << kReadbackExtent.width << 'x'
                      << kReadbackExtent.height << ", solid pixels " << metrics.solidPixels
                      << " (left " << metrics.solidLeftPixels << ", right "
                      << metrics.solidRightPixels << "), moved diff L/R "
                      << metrics.movedLeftDifference << '/' << metrics.movedRightDifference
                      << ", depth-order diff " << metrics.depthOrderDifference;
            if (state.wireframeAvailable) {
                std::cout << ", wire pixels " << metrics.wirePixels << ", cleared interior "
                          << metrics.clearedSolidInteriorPixels << '/'
                          << metrics.solidInteriorPixels << ", same-frame wire/solid diff "
                          << metrics.sameFrameWireDifference << '/'
                          << metrics.sameFrameSolidDifference
                          << ", wireframe path PolygonLine, fallback none";
            } else {
                std::cout
                    << ", wireframe capability unavailable, typed failure verified, fallback none";
            }
            std::cout << ", mesh " << kBasicValidationMeshResourceKey.value << ", material "
                      << kBasicDefaultUnlitMaterialResourceKey.value
                      << ", format cache scene/grid/lines created/reused "
                      << state.retainedFormatScenePipelineStats.created << '/'
                      << state.retainedFormatScenePipelineStats.reused << ','
                      << state.retainedFormatWorldGridPipelineStats.created << '/'
                      << state.retainedFormatWorldGridPipelineStats.reused << ','
                      << state.retainedFormatDebugLinePipelineStats.created << '/'
                      << state.retainedFormatDebugLinePipelineStats.reused
                      << ", unknown resource keys rejected\n";
        }

    } // namespace

    int runSmokeRenderViewSceneMesh() {
        auto glfw = GlfwInstance::create();
        if (!glfw) {
            logError(glfw.error().message);
            return EXIT_FAILURE;
        }
        auto extensions = glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            logError(extensions.error().message);
            return EXIT_FAILURE;
        }
        auto window = GlfwWindow::create(
            *glfw,
            WindowDesc{.title = "Asharia Engine RenderView Scene Mesh Smoke", .visible = false});
        if (!window) {
            logError(window.error().message);
            return EXIT_FAILURE;
        }

        auto context = VulkanContext::create(VulkanContextDesc{
            .applicationName = "Asharia Engine RenderView Scene Mesh Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = VulkanDebugLabelMode::Required,
            .externalInterop = {},
            .enableFillModeNonSolid = true,
        });
        if (!context) {
            logError(context.error().message);
            return EXIT_FAILURE;
        }

        GlfwWindow::pollEvents();
        const WindowFramebufferExtent framebuffer = window->framebufferExtent();
        auto frameLoop = VulkanFrameLoop::create(
            *context, VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        auto renderer = BasicFullscreenTextureRenderer::create(BasicFullscreenTextureRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = std::filesystem::path{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR},
            .deviceCapabilities = context->capabilities(),
        });
        if (!renderer) {
            logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        std::array<SceneMeshProbe, kProbeCount> probes;
        if (!createSceneMeshProbes(*context, probes)) {
            return EXIT_FAILURE;
        }

        SceneMeshSmokeRunState state{
            .context = *context,
            .window = *window,
            .frameLoop = *frameLoop,
            .renderer = *renderer,
            .probes = probes,
            .frameIndex = 1U,
            .wireframeAvailable = context->capabilities().fillModeNonSolid,
            .wireframeUnavailableReported = false,
            .unknownResources = {},
            .retainedFormatScenePipelineStats = {},
            .retainedFormatWorldGridPipelineStats = {},
            .retainedFormatDebugLinePipelineStats = {},
            .retainedFormatCacheVerified = false,
        };
        if (!recordAllSceneMeshProbes(state) || !waitForSceneMeshReadback(state)) {
            return EXIT_FAILURE;
        }

        SmokePixelMetrics metrics;
        if (!validateSceneMeshEvidence(state, metrics)) {
            return EXIT_FAILURE;
        }

        printSceneMeshMetrics(metrics, state);
        window->requestClose();
        return EXIT_SUCCESS;
    }

} // namespace asharia::sample_viewer

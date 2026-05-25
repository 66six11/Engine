#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "asharia/rendergraph/render_graph.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia {
    namespace renderer_basic_detail {

        [[nodiscard]] Error renderGraphError(std::string message) {
            return Error{ErrorDomain::RenderGraph, 0, std::move(message)};
        }

        struct BasicFullscreenPipelineKey {
            std::string shaderAsset;
            std::string shaderPass;
            std::string textureBinding;
            std::string textureSlot;
        };

        struct BasicMesh3DPushConstants {
            std::array<float, 4> mvpRow0{};
            std::array<float, 4> mvpRow1{};
            std::array<float, 4> mvpRow2{};
            std::array<float, 4> mvpRow3{};
        };

        constexpr std::uint32_t kBasicComputeValueCount = 4;

        struct BasicFullscreenTexturePassMessages {
            std::string_view paramsContext;
            std::string_view unknownTextureSlotMessage;
        };

#include "basic_renderers/render_view_diagnostics.inl"

        template <typename Params>
        [[nodiscard]] Result<Params> readPassParams(RenderGraphPassContext pass,
                                                    std::string_view expectedParamsType,
                                                    std::string_view context) {
            if (pass.paramsType != expectedParamsType) {
                return std::unexpected{
                    renderGraphError(std::string{context} + " expected params type '" +
                                     std::string{expectedParamsType} + "' but found '" +
                                     std::string{pass.paramsType} + "'")};
            }
            if (pass.paramsData.size() != sizeof(Params)) {
                return std::unexpected{
                    renderGraphError(std::string{context} + " expected params payload size " +
                                     std::to_string(sizeof(Params)) + " but found " +
                                     std::to_string(pass.paramsData.size()))};
            }

            Params params{};
            std::memcpy(&params, pass.paramsData.data(), sizeof(Params));
            return params;
        }

        [[nodiscard]] Result<void> expectCommandKind(const RenderGraphCommand& command,
                                                     RenderGraphCommandKind expected,
                                                     std::string_view context) {
            if (command.kind == expected) {
                return {};
            }

            return std::unexpected{
                renderGraphError(std::string{context} + " command kind mismatch")};
        }

        [[nodiscard]] BasicTransferClearParams
        basicTransferClearParams(VkClearColorValue clearColor) {
            return BasicTransferClearParams{
                .color =
                    {
                        clearColor.float32[0],
                        clearColor.float32[1],
                        clearColor.float32[2],
                        clearColor.float32[3],
                    },
            };
        }

        [[nodiscard]] VkClearColorValue basicClearColorValue(BasicTransferClearParams params) {
            return VkClearColorValue{{
                params.color[0],
                params.color[1],
                params.color[2],
                params.color[3],
            }};
        }

        [[nodiscard]] std::uint32_t basicRenderViewKindValue(BasicRenderViewKind kind) {
            switch (kind) {
            case BasicRenderViewKind::Game:
                return 0;
            case BasicRenderViewKind::Scene:
                return 1;
            case BasicRenderViewKind::Preview:
                return 2;
            }
            return 0;
        }

        [[nodiscard]] BasicRenderViewOverlayParams
        basicRenderViewOverlayParams(const BasicRenderViewDesc& view) {
            return BasicRenderViewOverlayParams{
                .cameraPositionNear =
                    {
                        view.camera.position[0],
                        view.camera.position[1],
                        view.camera.position[2],
                        view.camera.nearPlane,
                    },
                .frameTimeScale =
                    {
                        view.frameParams.timeSeconds,
                        view.frameParams.deltaSeconds,
                        view.frameParams.renderScale,
                        view.camera.farPlane,
                    },
                .debugWorldLineCount =
                    static_cast<std::uint32_t>(view.overlay.debugWorldLines.size()),
                .viewKind = basicRenderViewKindValue(view.viewKind),
                .overlayEnabled = view.overlay.enabled ? 1U : 0U,
                .reserved = 0,
            };
        }

        void accumulateBufferStats(VulkanBufferStats& total, const VulkanBuffer& buffer) {
            const VulkanBufferStats stats = buffer.stats();
            total.created += stats.created;
            total.hostUploadCreated += stats.hostUploadCreated;
            total.hostReadbackCreated += stats.hostReadbackCreated;
            total.deviceLocalCreated += stats.deviceLocalCreated;
            total.allocatedBytes += stats.allocatedBytes;
            total.uploadCalls += stats.uploadCalls;
            total.uploadedBytes += stats.uploadedBytes;
        }

        [[nodiscard]] Result<BasicFullscreenPipelineKey>
        basicFullscreenPipelineKey(RenderGraphPassContext pass) {
            if (pass.commands.size() != 4) {
                return std::unexpected{
                    renderGraphError("Fullscreen pass expected exactly four commands")};
            }

            const RenderGraphCommand& shader = pass.commands[0];
            const RenderGraphCommand& texture = pass.commands[1];
            const RenderGraphCommand& tint = pass.commands[2];
            const RenderGraphCommand& draw = pass.commands[3];

            auto shaderKind =
                expectCommandKind(shader, RenderGraphCommandKind::SetShader, "Fullscreen shader");
            if (!shaderKind) {
                return std::unexpected{std::move(shaderKind.error())};
            }
            auto textureKind = expectCommandKind(texture, RenderGraphCommandKind::SetTexture,
                                                 "Fullscreen texture");
            if (!textureKind) {
                return std::unexpected{std::move(textureKind.error())};
            }
            auto tintKind =
                expectCommandKind(tint, RenderGraphCommandKind::SetVec4, "Fullscreen tint");
            if (!tintKind) {
                return std::unexpected{std::move(tintKind.error())};
            }
            auto drawKind = expectCommandKind(draw, RenderGraphCommandKind::DrawFullscreenTriangle,
                                              "Fullscreen draw");
            if (!drawKind) {
                return std::unexpected{std::move(drawKind.error())};
            }

            if (shader.name.empty() || shader.secondaryName.empty()) {
                return std::unexpected{
                    renderGraphError("Fullscreen pass requires a shader asset and pass")};
            }
            if (shader.name != "Hidden/DescriptorLayout" || shader.secondaryName != "Fullscreen") {
                return std::unexpected{
                    renderGraphError("Fullscreen pass shader command does not match the current "
                                     "pipeline key")};
            }
            if (texture.name != "SourceTex" || texture.secondaryName != "source") {
                return std::unexpected{
                    renderGraphError("Fullscreen pass texture command must bind SourceTex "
                                     "from the source slot")};
            }
            if (tint.name != "Tint") {
                return std::unexpected{
                    renderGraphError("Fullscreen pass tint command must target Tint")};
            }

            return BasicFullscreenPipelineKey{
                .shaderAsset = shader.name,
                .shaderPass = shader.secondaryName,
                .textureBinding = texture.name,
                .textureSlot = texture.secondaryName,
            };
        }

        [[nodiscard]] Result<void>
        validateBasicRenderViewOverlayCommands(RenderGraphPassContext pass,
                                               BasicRenderViewOverlayParams params) {
            if (pass.commands.size() != 4) {
                return std::unexpected{
                    renderGraphError("RenderView overlay pass expected exactly four commands")};
            }

            const RenderGraphCommand& shader = pass.commands[0];
            const RenderGraphCommand& camera = pass.commands[1];
            const RenderGraphCommand& frameParams = pass.commands[2];
            const RenderGraphCommand& lineCount = pass.commands[3];

            auto shaderKind = expectCommandKind(shader, RenderGraphCommandKind::SetShader,
                                                "RenderView overlay shader");
            if (!shaderKind) {
                return std::unexpected{std::move(shaderKind.error())};
            }
            auto cameraKind = expectCommandKind(camera, RenderGraphCommandKind::SetVec4,
                                                "RenderView overlay camera constants");
            if (!cameraKind) {
                return std::unexpected{std::move(cameraKind.error())};
            }
            auto frameKind = expectCommandKind(frameParams, RenderGraphCommandKind::SetVec4,
                                               "RenderView overlay frame constants");
            if (!frameKind) {
                return std::unexpected{std::move(frameKind.error())};
            }
            auto lineKind = expectCommandKind(lineCount, RenderGraphCommandKind::SetInt,
                                              "RenderView overlay line count");
            if (!lineKind) {
                return std::unexpected{std::move(lineKind.error())};
            }

            if (shader.name != "Hidden/RenderViewOverlay" || shader.secondaryName != "Inputs") {
                return std::unexpected{
                    renderGraphError("RenderView overlay pass shader command does not match the "
                                     "current input contract")};
            }
            if (camera.name != "CameraPositionNear" ||
                camera.floatValues != params.cameraPositionNear) {
                return std::unexpected{
                    renderGraphError("RenderView overlay camera command does not match params")};
            }
            if (frameParams.name != "FrameTimeScale" ||
                frameParams.floatValues != params.frameTimeScale) {
                return std::unexpected{
                    renderGraphError("RenderView overlay frame command does not match params")};
            }
            if (lineCount.name != "DebugWorldLineCount" ||
                lineCount.intValue != static_cast<int>(params.debugWorldLineCount)) {
                return std::unexpected{
                    renderGraphError("RenderView overlay line count command does not match "
                                     "params")};
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateFullscreenTintCommand(RenderGraphPassContext pass,
                                      const BasicFullscreenParams& params) {
            if (pass.commands.size() != 4) {
                return std::unexpected{
                    renderGraphError("Fullscreen pass expected exactly four commands")};
            }

            const RenderGraphCommand& tint = pass.commands[2];
            if (tint.floatValues != params.tint) {
                return std::unexpected{
                    renderGraphError("Fullscreen tint command does not match typed params")};
            }

            return {};
        }

#include "basic_renderers/shader_contracts.inl"
#include "basic_renderers/pipeline_layouts.inl"
        void recordTransferClear(const VulkanFrameRecordContext& frame,
                                 VkClearColorValue clearColor) {
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(frame.commandBuffer, frame.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
        }

        void recordImageCopy(const VulkanFrameRecordContext& frame,
                             const VulkanRenderGraphImageBinding& source,
                             const VulkanRenderGraphImageBinding& target, VkExtent2D extent) {
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = source.aspectMask;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = target.aspectMask;
            copyRegion.dstSubresource.mipLevel = 0;
            copyRegion.dstSubresource.baseArrayLayer = 0;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = VkExtent3D{
                .width = extent.width,
                .height = extent.height,
                .depth = 1,
            };

            vkCmdCopyImage(frame.commandBuffer, source.vulkanImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.vulkanImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

        [[nodiscard]] std::array<VkVertexInputBindingDescription, 1> basicVertexInputBindings() {
            return std::array{
                VkVertexInputBindingDescription{
                    .binding = 0,
                    .stride = sizeof(BasicVertex),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                },
            };
        }

        [[nodiscard]] std::array<VkVertexInputAttributeDescription, 2>
        basicVertexInputAttributes() {
            return std::array{
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(BasicVertex, position),
                },
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(BasicVertex, color),
                },
            };
        }

        [[nodiscard]] std::array<VkVertexInputBindingDescription, 1> basicVertex3DInputBindings() {
            return std::array{
                VkVertexInputBindingDescription{
                    .binding = 0,
                    .stride = sizeof(BasicVertex3D),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                },
            };
        }

        [[nodiscard]] std::array<VkVertexInputAttributeDescription, 2>
        basicVertex3DInputAttributes() {
            return std::array{
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(BasicVertex3D, position),
                },
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(BasicVertex3D, color),
                },
            };
        }

        using BasicMat4 = BasicTransformMatrix3D;

        [[nodiscard]] constexpr float basicMat4At(const BasicMat4& matrix, std::size_t row,
                                                  std::size_t column) {
            return matrix.at((row * 4U) + column);
        }

        [[nodiscard]] BasicMat4 multiplyBasicMat4(const BasicMat4& lhs, const BasicMat4& rhs) {
            BasicMat4 result{};
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    float value = 0.0F;
                    for (std::size_t index = 0; index < 4; ++index) {
                        value += basicMat4At(lhs, row, index) * basicMat4At(rhs, index, column);
                    }
                    result.at((row * 4U) + column) = value;
                }
            }
            return result;
        }

        [[nodiscard]] BasicMat4 basicMesh3DModelMatrix() {
            constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
            const float yaw = 32.0F * kDegreesToRadians;
            const float pitch = -18.0F * kDegreesToRadians;
            const float yawCos = std::cos(yaw);
            const float yawSin = std::sin(yaw);
            const float pitchCos = std::cos(pitch);
            const float pitchSin = std::sin(pitch);

            const BasicMat4 rotateY{
                yawCos,  0.0F, yawSin, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                -yawSin, 0.0F, yawCos, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
            };
            const BasicMat4 rotateX{
                1.0F, 0.0F,     0.0F,     0.0F, 0.0F, pitchCos, -pitchSin, 0.0F,
                0.0F, pitchSin, pitchCos, 0.0F, 0.0F, 0.0F,     0.0F,      1.0F,
            };
            const BasicMat4 translate{
                1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 3.0F, 0.0F, 0.0F, 0.0F, 1.0F,
            };

            return multiplyBasicMat4(translate, multiplyBasicMat4(rotateY, rotateX));
        }

        [[nodiscard]] BasicMat4 basicMesh3DProjectionMatrix(VkExtent2D extent) {
            const float width = static_cast<float>(std::max(extent.width, 1U));
            const float height = static_cast<float>(std::max(extent.height, 1U));
            const float aspect = width / height;
            constexpr float kNear = 0.1F;
            constexpr float kFar = 32.0F;
            constexpr float kFovYRadians = 60.0F * std::numbers::pi_v<float> / 180.0F;
            const float focalLength = 1.0F / std::tan(kFovYRadians * 0.5F);

            return BasicMat4{
                focalLength / aspect,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                focalLength,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                kFar / (kFar - kNear),
                (-kNear * kFar) / (kFar - kNear),
                0.0F,
                0.0F,
                1.0F,
                0.0F,
            };
        }

        [[nodiscard]] BasicMesh3DPushConstants
        basicMesh3DPushConstants(VkExtent2D extent, const BasicMat4& modelMatrix) {
            const BasicMat4 mvp =
                multiplyBasicMat4(basicMesh3DProjectionMatrix(extent), modelMatrix);
            return BasicMesh3DPushConstants{
                .mvpRow0 = {mvp[0], mvp[1], mvp[2], mvp[3]},
                .mvpRow1 = {mvp[4], mvp[5], mvp[6], mvp[7]},
                .mvpRow2 = {mvp[8], mvp[9], mvp[10], mvp[11]},
                .mvpRow3 = {mvp[12], mvp[13], mvp[14], mvp[15]},
            };
        }

        [[nodiscard]] BasicMesh3DPushConstants
        basicMesh3DPushConstants(const BasicRenderViewCamera& camera,
                                 const BasicMat4& modelMatrix) {
            const BasicMat4 mvp = multiplyBasicMat4(
                BasicMat4{camera.viewProjection[0], camera.viewProjection[1],
                          camera.viewProjection[2], camera.viewProjection[3],
                          camera.viewProjection[4], camera.viewProjection[5],
                          camera.viewProjection[6], camera.viewProjection[7],
                          camera.viewProjection[8], camera.viewProjection[9],
                          camera.viewProjection[10], camera.viewProjection[11],
                          camera.viewProjection[12], camera.viewProjection[13],
                          camera.viewProjection[14], camera.viewProjection[15]},
                modelMatrix);
            return BasicMesh3DPushConstants{
                .mvpRow0 = {mvp[0], mvp[1], mvp[2], mvp[3]},
                .mvpRow1 = {mvp[4], mvp[5], mvp[6], mvp[7]},
                .mvpRow2 = {mvp[8], mvp[9], mvp[10], mvp[11]},
                .mvpRow3 = {mvp[12], mvp[13], mvp[14], mvp[15]},
            };
        }

        struct BasicDrawBuffers {
            VkBuffer vertex{VK_NULL_HANDLE};
            VkBuffer index{VK_NULL_HANDLE};
        };

        void recordTriangleDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                                BasicDrawBuffers buffers, BasicDrawItem drawItem,
                                VkImageView depthImageView = VK_NULL_HANDLE) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = frame.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depthImageView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue = VkClearValue{
                .depthStencil =
                    VkClearDepthStencilValue{
                        .depth = 1.0F,
                        .stencil = 0,
                    },
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            if (depthImageView != VK_NULL_HANDLE) {
                renderingInfo.pDepthAttachment = &depthAttachment;
            }

            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(frame.extent.width),
                .height = static_cast<float>(frame.extent.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            constexpr VkDeviceSize vertexBufferOffset = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffers.vertex, &vertexBufferOffset);
            if (drawItem.indexCount > 0) {
                vkCmdBindIndexBuffer(frame.commandBuffer, buffers.index, 0, VK_INDEX_TYPE_UINT16);
            }
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            if (drawItem.indexCount > 0) {
                vkCmdDrawIndexed(frame.commandBuffer, drawItem.indexCount, drawItem.instanceCount,
                                 drawItem.firstIndex, drawItem.vertexOffset,
                                 drawItem.firstInstance);
            } else {
                vkCmdDraw(frame.commandBuffer, drawItem.vertexCount, drawItem.instanceCount,
                          drawItem.firstVertex, drawItem.firstInstance);
            }
            vkCmdEndRendering(frame.commandBuffer);
        }

        void recordMesh3DDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                              VkPipelineLayout pipelineLayout, BasicDrawBuffers buffers,
                              VkImageView depthImageView, BasicDrawItem drawItem,
                              const BasicRenderViewCamera* camera = nullptr) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = frame.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depthImageView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue = VkClearValue{
                .depthStencil =
                    VkClearDepthStencilValue{
                        .depth = 1.0F,
                        .stencil = 0,
                    },
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(frame.extent.width),
                .height = static_cast<float>(frame.extent.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };
            const BasicMesh3DPushConstants pushConstants = camera != nullptr
                ? basicMesh3DPushConstants(*camera, basicMesh3DModelMatrix())
                : basicMesh3DPushConstants(frame.extent, basicMesh3DModelMatrix());

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               static_cast<std::uint32_t>(sizeof(pushConstants)), &pushConstants);
            constexpr VkDeviceSize vertexBufferOffset = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffers.vertex, &vertexBufferOffset);
            vkCmdBindIndexBuffer(frame.commandBuffer, buffers.index, 0, VK_INDEX_TYPE_UINT16);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDrawIndexed(frame.commandBuffer, drawItem.indexCount, drawItem.instanceCount,
                             drawItem.firstIndex, drawItem.vertexOffset, drawItem.firstInstance);
            vkCmdEndRendering(frame.commandBuffer);
        }

        void recordDrawListDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                                VkPipelineLayout pipelineLayout, BasicDrawBuffers buffers,
                                VkImageView depthImageView,
                                std::span<const BasicDrawListItem> drawItems) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = frame.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depthImageView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue = VkClearValue{
                .depthStencil =
                    VkClearDepthStencilValue{
                        .depth = 1.0F,
                        .stencil = 0,
                    },
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(frame.extent.width),
                .height = static_cast<float>(frame.extent.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            constexpr VkDeviceSize vertexBufferOffset = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffers.vertex, &vertexBufferOffset);
            vkCmdBindIndexBuffer(frame.commandBuffer, buffers.index, 0, VK_INDEX_TYPE_UINT16);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

            for (const BasicDrawListItem& item : drawItems) {
                const BasicMesh3DPushConstants pushConstants =
                    basicMesh3DPushConstants(frame.extent, item.modelMatrix);
                vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, static_cast<std::uint32_t>(sizeof(pushConstants)),
                                   &pushConstants);

                if (item.drawItem.indexCount > 0) {
                    vkCmdDrawIndexed(frame.commandBuffer, item.drawItem.indexCount,
                                     item.drawItem.instanceCount, item.drawItem.firstIndex,
                                     item.drawItem.vertexOffset, item.drawItem.firstInstance);
                } else {
                    vkCmdDraw(frame.commandBuffer, item.drawItem.vertexCount,
                              item.drawItem.instanceCount, item.drawItem.firstVertex,
                              item.drawItem.firstInstance);
                }
            }

            vkCmdEndRendering(frame.commandBuffer);
        }

        void recordFullscreenTextureDraw(const VulkanFrameRecordContext& frame,
                                         VkImageView targetImageView, VkExtent2D targetExtent,
                                         VkPipeline pipeline, VkPipelineLayout pipelineLayout,
                                         VkDescriptorSet descriptorSet) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = targetImageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = VkClearValue{
                .color = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = targetExtent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(targetExtent.width),
                .height = static_cast<float>(targetExtent.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = targetExtent,
            };

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(frame.commandBuffer);
        }

        [[nodiscard]] Result<VkClearValue> basicMrtClearValue(const RenderGraphCommand& command,
                                                              std::string_view slotName) {
            auto commandKind =
                expectCommandKind(command, RenderGraphCommandKind::ClearColor, "MRT clear");
            if (!commandKind) {
                return std::unexpected{std::move(commandKind.error())};
            }
            if (command.name != slotName) {
                return std::unexpected{
                    renderGraphError("MRT clear command does not match its color slot")};
            }

            return VkClearValue{
                .color = VkClearColorValue{{
                    command.floatValues[0],
                    command.floatValues[1],
                    command.floatValues[2],
                    command.floatValues[3],
                }},
            };
        }

        [[nodiscard]] Result<std::array<VkClearValue, 2>>
        basicMrtClearValues(RenderGraphPassContext pass) {
            constexpr std::size_t kAttachmentCount = 2;
            if (pass.commands.size() != kAttachmentCount) {
                return std::unexpected{
                    renderGraphError("MRT pass expected exactly two clear commands")};
            }

            auto color0 = basicMrtClearValue(pass.commands[0], "color0");
            if (!color0) {
                return std::unexpected{std::move(color0.error())};
            }
            auto color1 = basicMrtClearValue(pass.commands[1], "color1");
            if (!color1) {
                return std::unexpected{std::move(color1.error())};
            }

            return std::array{*color0, *color1};
        }

        void recordMrtClear(const VulkanFrameRecordContext& frame,
                            const std::array<VulkanRenderGraphImageBinding, 2>& colors,
                            const std::array<VkClearValue, 2>& clearValues) {
            const std::array colorAttachments{
                VkRenderingAttachmentInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext = nullptr,
                    .imageView = colors[0].vulkanImageView,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE,
                    .resolveImageView = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = clearValues[0],
                },
                VkRenderingAttachmentInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext = nullptr,
                    .imageView = colors[1].vulkanImageView,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE,
                    .resolveImageView = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = clearValues[1],
                },
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = frame.extent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount =
                static_cast<std::uint32_t>(colorAttachments.size());
            renderingInfo.pColorAttachments = colorAttachments.data();

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdEndRendering(frame.commandBuffer);
        }

        [[nodiscard]] VkAttachmentLoadOp
        basicRenderViewOverlayLoadOp(BasicRenderViewOverlayColorLoadOp colorLoadOp) {
            switch (colorLoadOp) {
            case BasicRenderViewOverlayColorLoadOp::LoadSceneColor:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case BasicRenderViewOverlayColorLoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        }

        [[nodiscard]] VkAttachmentStoreOp
        basicRenderViewOverlayStoreOp(BasicRenderViewOverlayColorStoreOp colorStoreOp) {
            switch (colorStoreOp) {
            case BasicRenderViewOverlayColorStoreOp::Store:
                return VK_ATTACHMENT_STORE_OP_STORE;
            case BasicRenderViewOverlayColorStoreOp::Discard:
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_STORE_OP_STORE;
        }

        void recordBasicRenderViewOverlayTouch(const VulkanFrameRecordContext& frame,
                                               VkImageView targetImageView,
                                               VkExtent2D targetExtent,
                                               BasicRenderViewOverlayColorLoadOp loadOp,
                                               BasicRenderViewOverlayColorStoreOp storeOp) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = targetImageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = basicRenderViewOverlayLoadOp(loadOp);
            colorAttachment.storeOp = basicRenderViewOverlayStoreOp(storeOp);
            colorAttachment.clearValue = VkClearValue{
                .color = VkClearColorValue{{0.0F, 0.0F, 0.0F, 0.0F}},
            };

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = VkRect2D{
                .offset = VkOffset2D{.x = 0, .y = 0},
                .extent = targetExtent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdEndRendering(frame.commandBuffer);
        }

        [[nodiscard]] Result<void>
        updateBasicFullscreenSourceDescriptor(VkDevice device, VkDescriptorSet descriptorSet,
                                              VkImageView sourceImageView) {
            if (descriptorSet == VK_NULL_HANDLE || sourceImageView == VK_NULL_HANDLE) {
                return std::unexpected{
                    Error{ErrorDomain::Vulkan, 0,
                          "Cannot update fullscreen source descriptor from incomplete inputs"}};
            }

            const std::array imageWrites{
                VulkanDescriptorImageWrite{
                    .descriptorSet = descriptorSet,
                    .binding = 1,
                    .arrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .imageView = sourceImageView,
                    .sampler = VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
            };
            updateVulkanDescriptorImages(device, imageWrites);
            return {};
        }

        [[nodiscard]] Result<void>
        executeBasicFullscreenSourceClear(const VulkanFrameRecordContext& frame,
                                          RenderGraphPassContext pass,
                                          std::span<const VulkanRenderGraphImageBinding> bindings,
                                          BasicRenderViewExecutionEventRecorder* eventRecorder) {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(frame, pass.name);
            if (eventRecorder != nullptr) {
                eventRecorder->beginPass(pass);
            }
            auto transitions =
                recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            auto clearParams = readPassParams<BasicTransferClearParams>(
                pass, kBasicTransferClearParamsType, "Fullscreen source clear pass");
            if (!clearParams) {
                return std::unexpected{std::move(clearParams.error())};
            }

            auto sourceBinding = findVulkanRenderGraphTransferWrite(pass, "target", bindings);
            if (!sourceBinding) {
                return std::unexpected{std::move(sourceBinding.error())};
            }

            const VkClearColorValue sourceColor = basicClearColorValue(*clearParams);
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = sourceBinding->aspectMask;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(frame.commandBuffer, sourceBinding->vulkanImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &sourceColor, 1,
                                 &clearRange);
            if (eventRecorder != nullptr) {
                eventRecorder->append(pass, BasicRenderViewExecutionEventKind::ClearColor,
                                      "ClearColor target",
                                      firstCommandIndex(pass, RenderGraphCommandKind::ClearColor),
                                      {}, {}, std::nullopt, sourceBinding->image.index);
                eventRecorder->endPass(pass);
            }
            return {};
        }

        [[nodiscard]] Result<void> executeBasicFullscreenTexturePass(
            const VulkanFrameRecordContext& frame, RenderGraphPassContext pass,
            std::span<const VulkanRenderGraphImageBinding> bindings, VkDevice device,
            VkPipeline pipeline, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet,
            VkExtent2D targetExtent, BasicFullscreenTexturePassMessages messages,
            BasicRenderViewExecutionEventRecorder* eventRecorder) {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(frame, pass.name);
            if (eventRecorder != nullptr) {
                eventRecorder->beginPass(pass);
            }
            auto transitions =
                recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            auto fullscreenParams = readPassParams<BasicFullscreenParams>(
                pass, kBasicRasterFullscreenParamsType, messages.paramsContext);
            if (!fullscreenParams) {
                return std::unexpected{std::move(fullscreenParams.error())};
            }

            auto pipelineKey = basicFullscreenPipelineKey(pass);
            if (!pipelineKey) {
                return std::unexpected{std::move(pipelineKey.error())};
            }
            auto tintCommand = validateFullscreenTintCommand(pass, *fullscreenParams);
            if (!tintCommand) {
                return std::unexpected{std::move(tintCommand.error())};
            }
            if (pipelineKey->textureSlot != "source") {
                return std::unexpected{
                    renderGraphError(std::string{messages.unknownTextureSlotMessage})};
            }

            auto sourceBinding = findVulkanRenderGraphShaderRead(pass, "source", bindings);
            if (!sourceBinding) {
                return std::unexpected{std::move(sourceBinding.error())};
            }
            auto descriptor = updateBasicFullscreenSourceDescriptor(device, descriptorSet,
                                                                    sourceBinding->vulkanImageView);
            if (!descriptor) {
                return std::unexpected{std::move(descriptor.error())};
            }

            auto targetBinding = findVulkanRenderGraphColorWrite(pass, "target", bindings);
            if (!targetBinding) {
                return std::unexpected{std::move(targetBinding.error())};
            }

            recordFullscreenTextureDraw(frame, targetBinding->vulkanImageView, targetExtent,
                                        pipeline, pipelineLayout, descriptorSet);
            if (eventRecorder != nullptr) {
                eventRecorder->append(
                    pass, BasicRenderViewExecutionEventKind::DrawFullscreenTriangle,
                    "DrawFullscreenTriangle",
                    firstCommandIndex(pass, RenderGraphCommandKind::DrawFullscreenTriangle),
                    BasicRenderViewDrawEvent{
                        .vertexCount = 3,
                        .indexCount = 0,
                        .instanceCount = 1,
                    },
                    {}, sourceBinding->image.index, targetBinding->image.index);
                eventRecorder->endPass(pass);
            }
            return {};
        }

        [[nodiscard]] Result<void> executeBasicRenderViewOverlayPass(
            const VulkanFrameRecordContext& frame, RenderGraphPassContext pass,
            std::span<const VulkanRenderGraphImageBinding> bindings, VkExtent2D targetExtent,
            BasicRenderViewOverlayColorLoadOp colorLoadOp,
            BasicRenderViewOverlayColorStoreOp colorStoreOp,
            BasicRenderViewExecutionEventRecorder* eventRecorder) {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(frame, pass.name);
            if (eventRecorder != nullptr) {
                eventRecorder->beginPass(pass);
            }
            auto transitions =
                recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            auto overlayParams = readPassParams<BasicRenderViewOverlayParams>(
                pass, kBasicRenderViewOverlayParamsType, "RenderView overlay pass");
            if (!overlayParams) {
                return std::unexpected{std::move(overlayParams.error())};
            }
            auto commands = validateBasicRenderViewOverlayCommands(pass, *overlayParams);
            if (!commands) {
                return std::unexpected{std::move(commands.error())};
            }

            auto targetBinding = findVulkanRenderGraphColorWrite(pass, "target", bindings);
            if (!targetBinding) {
                return std::unexpected{std::move(targetBinding.error())};
            }

            recordBasicRenderViewOverlayTouch(frame, targetBinding->vulkanImageView,
                                              targetExtent, colorLoadOp, colorStoreOp);
            if (eventRecorder != nullptr) {
                eventRecorder->append(
                    pass, BasicRenderViewExecutionEventKind::RenderViewInput,
                    "BindRenderViewInputs",
                    firstCommandIndex(pass, RenderGraphCommandKind::SetShader), {}, {},
                    std::nullopt, targetBinding->image.index);
                eventRecorder->endPass(pass);
            }
            return {};
        }

        [[nodiscard]] Result<void> validateBasicComputeCommands(RenderGraphPassContext pass,
                                                                BasicComputeDispatchParams params) {
            if (pass.commands.size() != 2) {
                return std::unexpected{
                    renderGraphError("Compute dispatch pass expected exactly two commands")};
            }

            const RenderGraphCommand& shader = pass.commands[0];
            const RenderGraphCommand& dispatch = pass.commands[1];
            auto shaderKind =
                expectCommandKind(shader, RenderGraphCommandKind::SetShader, "Compute shader");
            if (!shaderKind) {
                return std::unexpected{std::move(shaderKind.error())};
            }
            auto dispatchKind =
                expectCommandKind(dispatch, RenderGraphCommandKind::Dispatch, "Compute dispatch");
            if (!dispatchKind) {
                return std::unexpected{std::move(dispatchKind.error())};
            }
            if (shader.name != "Hidden/BasicCompute" || shader.secondaryName != "Main") {
                return std::unexpected{
                    renderGraphError("Compute dispatch shader command does not match the current "
                                     "pipeline key")};
            }
            if (dispatch.uintValues != std::array<std::uint32_t, 3>{
                                           params.groupCountX,
                                           params.groupCountY,
                                           params.groupCountZ,
                                       }) {
                return std::unexpected{
                    renderGraphError("Compute dispatch command does not match typed params")};
            }
            return {};
        }

        [[nodiscard]] Result<void> executeBasicComputeDispatchPass(
            const VulkanFrameRecordContext& frame, RenderGraphPassContext pass,
            std::span<const VulkanRenderGraphImageBinding> imageBindings,
            std::span<const VulkanRenderGraphBufferBinding> bufferBindings, VkPipeline pipeline,
            VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet,
            BasicComputeDispatchStats& stats) {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(
                frame, renderGraphPassDebugLabel(pass, imageBindings, bufferBindings));

            auto transitions = recordRenderGraphBufferTransitions(
                frame, pass.bufferTransitionsBefore, bufferBindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            auto params = readPassParams<BasicComputeDispatchParams>(
                pass, kBasicComputeDispatchParamsType, "Compute dispatch pass");
            if (!params) {
                return std::unexpected{std::move(params.error())};
            }
            auto commands = validateBasicComputeCommands(pass, *params);
            if (!commands) {
                return std::unexpected{std::move(commands.error())};
            }

            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdDispatch(frame.commandBuffer, params->groupCountX, params->groupCountY,
                          params->groupCountZ);
            ++stats.dispatchesRecorded;

            return {};
        }

        [[nodiscard]] Result<void> executeBasicComputeReadbackPass(
            const VulkanFrameRecordContext& frame, RenderGraphPassContext pass,
            std::span<const VulkanRenderGraphImageBinding> imageBindings,
            std::span<const VulkanRenderGraphBufferBinding> bufferBindings) {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(
                frame, renderGraphPassDebugLabel(pass, imageBindings, bufferBindings));

            auto transitions = recordRenderGraphBufferTransitions(
                frame, pass.bufferTransitionsBefore, bufferBindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }
            if (!pass.commands.empty()) {
                return std::unexpected{
                    renderGraphError("Compute readback copy pass expected no commands")};
            }

            auto source = findVulkanRenderGraphBufferTransferRead(pass, "source", bufferBindings);
            if (!source) {
                return std::unexpected{std::move(source.error())};
            }
            auto target = findVulkanRenderGraphBufferTransferWrite(pass, "target", bufferBindings);
            if (!target) {
                return std::unexpected{std::move(target.error())};
            }
            if (source->size == VK_WHOLE_SIZE || target->size == VK_WHOLE_SIZE ||
                source->size > target->size) {
                return std::unexpected{
                    renderGraphError("Compute readback copy buffer bindings have invalid sizes")};
            }

            VkBufferCopy copy{};
            copy.srcOffset = source->offset;
            copy.dstOffset = target->offset;
            copy.size = source->size;
            vkCmdCopyBuffer(frame.commandBuffer, source->vulkanBuffer, target->vulkanBuffer, 1,
                            &copy);
            return {};
        }

        [[nodiscard]] bool usesImage(std::span<const RenderGraphImageHandle> images,
                                     RenderGraphImageHandle image) {
            return std::ranges::any_of(
                images, [image](RenderGraphImageHandle used) { return used == image; });
        }

        [[nodiscard]] VkImageUsageFlags
        transientUsageFlags(const RenderGraphCompileResult& compiled,
                            RenderGraphImageHandle image) {
            VkImageUsageFlags usage{};
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                if (usesImage(pass.transferWrites, image)) {
                    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                }
                if (usesImage(pass.transferReads, image)) {
                    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                }
                if (usesImage(pass.colorWrites, image)) {
                    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }
                if (usesImage(pass.shaderReads, image) ||
                    usesImage(pass.depthSampledReads, image)) {
                    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }
                if (usesImage(pass.depthReads, image) || usesImage(pass.depthWrites, image)) {
                    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }
            }
            return usage;
        }

        [[nodiscard]] Result<void>
        releaseTransientResources(const VulkanFrameRecordContext& frame,
                                  VulkanTransientImagePool& transientImagePool,
                                  std::vector<VulkanTransientImageResource>& transientImages) {
            for (VulkanTransientImageResource& resource : transientImages) {
                auto released = transientImagePool.release(frame, resource);
                if (!released) {
                    return std::unexpected{std::move(released.error())};
                }
            }

            transientImages.clear();
            return {};
        }

        [[nodiscard]] Result<void>
        prepareTransientResources(const VulkanFrameRecordContext& frame, VkDevice device,
                                  VmaAllocator allocator, const RenderGraphCompileResult& compiled,
                                  std::vector<VulkanRenderGraphImageBinding>& bindings,
                                  VulkanTransientImagePool& transientImagePool,
                                  std::vector<VulkanTransientImageResource>& transientImages) {
            auto released = releaseTransientResources(frame, transientImagePool, transientImages);
            if (!released) {
                return std::unexpected{std::move(released.error())};
            }

            for (const RenderGraphTransientImageAllocation& allocation : compiled.transientImages) {
                const VkFormat format = vulkanFormat(allocation.format);
                const VkImageAspectFlags aspectMask =
                    basicRenderGraphImageAspect(allocation.format);
                const VkImageUsageFlags usage = transientUsageFlags(compiled, allocation.image);

                auto resource = transientImagePool.acquire(VulkanImageDesc{
                    .device = device,
                    .allocator = allocator,
                    .format = format,
                    .extent =
                        VkExtent2D{
                            .width = allocation.extent.width,
                            .height = allocation.extent.height,
                        },
                    .usage = usage,
                    .aspectMask = aspectMask,
                });
                if (!resource) {
                    return std::unexpected{std::move(resource.error())};
                }

                bindings.push_back(VulkanRenderGraphImageBinding{
                    .image = allocation.image,
                    .vulkanImage = resource->image.handle(),
                    .vulkanImageView = resource->imageView.handle(),
                    .aspectMask = resource->image.aspectMask(),
                    .debugName = allocation.imageName,
                });
                auto named = setVulkanRenderGraphImageDebugNames(frame, bindings.back());
                if (!named) {
                    return std::unexpected{std::move(named.error())};
                }
                transientImages.push_back(std::move(*resource));
            }

            return {};
        }

#include "basic_renderers/graph_recording.inl"
#include "basic_renderers/render_view_targets.inl"
#include "basic_renderers/debug_preview.inl"
        [[nodiscard]] Result<void>
        validateBasicDrawListItems(std::span<const BasicDrawListItem> drawItems) {
            if (drawItems.empty()) {
                return std::unexpected{
                    Error{ErrorDomain::Vulkan, 0, "Draw list renderer requires at least one item"}};
            }

            constexpr auto vertices = basicCubeVertices();
            constexpr auto indices = basicCubeIndices();

            for (const BasicDrawListItem& item : drawItems) {
                if ((item.drawItem.vertexCount == 0 && item.drawItem.indexCount == 0) ||
                    item.drawItem.instanceCount == 0) {
                    return std::unexpected{
                        Error{ErrorDomain::Vulkan, 0,
                              "Draw list renderer item must draw at least one vertex or index"}};
                }

                if (item.drawItem.indexCount > 0) {
                    const std::size_t firstIndex = item.drawItem.firstIndex;
                    const std::size_t indexCount = item.drawItem.indexCount;
                    if (firstIndex > indices.size() || indexCount > indices.size() - firstIndex) {
                        return std::unexpected{
                            Error{ErrorDomain::Vulkan, 0,
                                  "Draw list renderer item index range exceeds cube index buffer"}};
                    }
                    if (item.drawItem.vertexOffset < 0) {
                        return std::unexpected{
                            Error{ErrorDomain::Vulkan, 0,
                                  "Draw list renderer item cannot use a negative vertex offset"}};
                    }

                    std::uint32_t maxIndex{};
                    for (const auto indexValue :
                         std::span{indices}.subspan(firstIndex, indexCount)) {
                        maxIndex = std::max(maxIndex, static_cast<std::uint32_t>(indexValue));
                    }
                    const std::size_t maxVertex =
                        static_cast<std::size_t>(item.drawItem.vertexOffset) + maxIndex;
                    if (maxVertex >= vertices.size()) {
                        return std::unexpected{Error{
                            ErrorDomain::Vulkan, 0,
                            "Draw list renderer item vertex range exceeds cube vertex buffer"}};
                    }
                    continue;
                }

                const std::size_t firstVertex = item.drawItem.firstVertex;
                const std::size_t vertexCount = item.drawItem.vertexCount;
                if (firstVertex > vertices.size() || vertexCount > vertices.size() - firstVertex) {
                    return std::unexpected{
                        Error{ErrorDomain::Vulkan, 0,
                              "Draw list renderer item vertex range exceeds cube vertex buffer"}};
                }
            }

            return {};
        }

        [[nodiscard]] Result<VulkanPipelineCache> createBasicPipelineCache(VkDevice device) {
            return VulkanPipelineCache::create(VulkanPipelineCacheDesc{
                .device = device,
                .initialData = {},
            });
        }

    } // namespace renderer_basic_detail

    using namespace asharia::renderer_basic_detail;

    // Keep the renderer implementations in private source parts. They share the helpers above
    // without promoting those helpers to a public or cross-translation-unit API.
#include "basic_renderers/descriptor_layout_smoke.inl"
#include "basic_renderers/fullscreen_texture_renderer.inl"
#include "basic_renderers/mrt_renderer.inl"
#include "basic_renderers/compute_dispatch_renderer.inl"
#include "basic_renderers/triangle_renderer.inl"
#include "basic_renderers/mesh3d_renderer.inl"
#include "basic_renderers/draw_list_renderer.inl"
} // namespace asharia

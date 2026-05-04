#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/shader_slang/reflection.hpp"

namespace vke {
    namespace {

        [[nodiscard]] Error shaderError(std::string message) {
            return Error{ErrorDomain::Shader, 0, std::move(message)};
        }

        [[nodiscard]] Result<void> expectString(std::string_view actual, std::string_view expected,
                                                std::string_view context) {
            if (actual == expected) {
                return {};
            }

            return std::unexpected{shaderError(std::string{context} + " expected '" +
                                               std::string{expected} + "' but found '" +
                                               std::string{actual} + "'")};
        }

        [[nodiscard]] Result<void> expectUint(std::uint32_t actual, std::uint32_t expected,
                                              std::string_view context) {
            if (actual == expected) {
                return {};
            }

            return std::unexpected{shaderError(std::string{context} + " expected " +
                                               std::to_string(expected) + " but found " +
                                               std::to_string(actual))};
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>>
        readSpirvFile(const std::filesystem::path& path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file) {
                return std::unexpected{shaderError("Failed to open SPIR-V file: " + path.string())};
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
                return std::unexpected{
                    shaderError("SPIR-V file size is invalid: " + path.string())};
            }

            std::vector<char> bytes(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(bytes.data(), size)) {
                return std::unexpected{shaderError("Failed to read SPIR-V file: " + path.string())};
            }

            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

        [[nodiscard]] const ShaderVertexInputReflection*
        findVertexInput(const ShaderReflection& reflection, std::string_view semantic,
                        std::uint32_t semanticIndex) {
            for (const ShaderVertexInputReflection& input : reflection.vertexInputs) {
                if (input.semantic == semantic && input.semanticIndex == semanticIndex) {
                    return &input;
                }
            }

            return nullptr;
        }

        [[nodiscard]] Result<void>
        validateVertexInput(const ShaderReflection& reflection, std::string_view semantic,
                            std::uint32_t semanticIndex, std::uint32_t expectedLocation,
                            std::string_view expectedScalarType, std::uint32_t expectedRowCount,
                            std::uint32_t expectedColumnCount) {
            const ShaderVertexInputReflection* input =
                findVertexInput(reflection, semantic, semanticIndex);
            if (input == nullptr) {
                return std::unexpected{
                    shaderError("Missing shader vertex input semantic: " + std::string{semantic} +
                                std::to_string(semanticIndex))};
            }

            auto location = expectUint(input->location, expectedLocation,
                                       "Shader vertex input " + std::string{semantic} +
                                           std::to_string(semanticIndex) + " location");
            if (!location) {
                return std::unexpected{std::move(location.error())};
            }
            auto scalarType = expectString(input->scalarType, expectedScalarType,
                                           "Shader vertex input " + std::string{semantic} +
                                               std::to_string(semanticIndex) + " scalarType");
            if (!scalarType) {
                return std::unexpected{std::move(scalarType.error())};
            }
            auto rowCount = expectUint(input->rowCount, expectedRowCount,
                                       "Shader vertex input " + std::string{semantic} +
                                           std::to_string(semanticIndex) + " rowCount");
            if (!rowCount) {
                return std::unexpected{std::move(rowCount.error())};
            }
            auto columnCount = expectUint(input->columnCount, expectedColumnCount,
                                          "Shader vertex input " + std::string{semantic} +
                                              std::to_string(semanticIndex) + " columnCount");
            if (!columnCount) {
                return std::unexpected{std::move(columnCount.error())};
            }

            return {};
        }

        [[nodiscard]] Result<void> validateNoResourceBindings(const ShaderReflection& reflection,
                                                              std::string_view shaderName) {
            auto descriptorCount =
                expectUint(reflection.descriptorBindingCount, 0,
                           std::string{shaderName} + " descriptor binding count");
            if (!descriptorCount) {
                return std::unexpected{std::move(descriptorCount.error())};
            }

            auto pushConstantCount = expectUint(reflection.pushConstantCount, 0,
                                                std::string{shaderName} + " push constant count");
            if (!pushConstantCount) {
                return std::unexpected{std::move(pushConstantCount.error())};
            }

            return {};
        }

        struct ShaderStageQuery {
            std::string_view stageVisibility;
            std::string_view context;
        };

        [[nodiscard]] Result<VkShaderStageFlags> shaderStageFlags(ShaderStageQuery query) {
            VkShaderStageFlags flags{};
            std::size_t begin = 0;
            while (begin <= query.stageVisibility.size()) {
                const std::size_t end = query.stageVisibility.find('|', begin);
                const std::size_t tokenEnd =
                    end == std::string_view::npos ? query.stageVisibility.size() : end;
                const std::string_view token =
                    query.stageVisibility.substr(begin, tokenEnd - begin);

                if (token == "vertex") {
                    flags |= VK_SHADER_STAGE_VERTEX_BIT;
                } else if (token == "fragment") {
                    flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
                } else if (token == "compute") {
                    flags |= VK_SHADER_STAGE_COMPUTE_BIT;
                } else if (!token.empty()) {
                    return std::unexpected{shaderError("Unsupported shader stage visibility for " +
                                                       std::string{query.context} + ": " +
                                                       std::string{token})};
                }

                if (end == std::string_view::npos) {
                    break;
                }
                begin = end + 1;
            }

            if (flags == 0) {
                return std::unexpected{shaderError("Missing shader stage visibility for " +
                                                   std::string{query.context})};
            }

            return flags;
        }

        [[nodiscard]] Result<VkDescriptorType>
        descriptorType(const ShaderDescriptorBindingReflection& binding) {
            if (binding.kind == "constantBuffer") {
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            }
            if (binding.kind == "texture") {
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            }
            if (binding.kind == "sampler") {
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
            if (binding.kind == "combinedTextureSampler") {
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            if (binding.kind == "mutableTexture") {
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            if (binding.kind == "typedBuffer" || binding.kind == "mutableTypedBuffer" ||
                binding.kind == "rawBuffer" || binding.kind == "mutableRawBuffer") {
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }

            return std::unexpected{shaderError("Unsupported descriptor binding kind for " +
                                               binding.name + ": " + binding.kind)};
        }

        struct PipelineLayoutResources {
            std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts;
            VulkanPipelineLayout pipelineLayout;
        };

        [[nodiscard]] Result<PipelineLayoutResources>
        createPipelineLayoutResources(VkDevice device, const ShaderResourceSignature& signature) {
            std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsBySet;
            for (const ShaderDescriptorBindingReflection& binding : signature.descriptorBindings) {
                if (binding.count == 0) {
                    return std::unexpected{
                        shaderError("Descriptor binding count must be non-zero: " + binding.name)};
                }

                if (binding.set >= bindingsBySet.size()) {
                    bindingsBySet.resize(static_cast<std::size_t>(binding.set) + 1);
                }

                auto type = descriptorType(binding);
                if (!type) {
                    return std::unexpected{std::move(type.error())};
                }
                auto stages = shaderStageFlags(ShaderStageQuery{
                    .stageVisibility = binding.stageVisibility,
                    .context = "descriptor " + binding.name,
                });
                if (!stages) {
                    return std::unexpected{std::move(stages.error())};
                }

                bindingsBySet[binding.set].push_back(VkDescriptorSetLayoutBinding{
                    .binding = binding.binding,
                    .descriptorType = *type,
                    .descriptorCount = binding.count,
                    .stageFlags = *stages,
                    .pImmutableSamplers = nullptr,
                });
            }

            for (std::vector<VkDescriptorSetLayoutBinding>& setBindings : bindingsBySet) {
                std::ranges::sort(setBindings, {}, &VkDescriptorSetLayoutBinding::binding);
            }

            PipelineLayoutResources resources;
            resources.descriptorSetLayouts.reserve(bindingsBySet.size());
            std::vector<VkDescriptorSetLayout> setLayoutHandles;
            setLayoutHandles.reserve(bindingsBySet.size());
            for (const std::vector<VkDescriptorSetLayoutBinding>& setBindings : bindingsBySet) {
                auto setLayout = VulkanDescriptorSetLayout::create(VulkanDescriptorSetLayoutDesc{
                    .device = device,
                    .bindings = setBindings,
                });
                if (!setLayout) {
                    return std::unexpected{std::move(setLayout.error())};
                }

                setLayoutHandles.push_back(setLayout->handle());
                resources.descriptorSetLayouts.push_back(std::move(*setLayout));
            }

            std::vector<VkPushConstantRange> pushConstantRanges;
            pushConstantRanges.reserve(signature.pushConstants.size());
            for (const ShaderPushConstantReflection& pushConstant : signature.pushConstants) {
                if (pushConstant.size == 0) {
                    return std::unexpected{shaderError(
                        "Push constant range size must be non-zero: " + pushConstant.name)};
                }
                auto stages = shaderStageFlags(ShaderStageQuery{
                    .stageVisibility = pushConstant.stageVisibility,
                    .context = "push constant " + pushConstant.name,
                });
                if (!stages) {
                    return std::unexpected{std::move(stages.error())};
                }

                pushConstantRanges.push_back(VkPushConstantRange{
                    .stageFlags = *stages,
                    .offset = pushConstant.offset,
                    .size = pushConstant.size,
                });
            }

            auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
                .device = device,
                .setLayouts = setLayoutHandles,
                .pushConstantRanges = pushConstantRanges,
            });
            if (!pipelineLayout) {
                return std::unexpected{std::move(pipelineLayout.error())};
            }

            resources.pipelineLayout = std::move(*pipelineLayout);
            return resources;
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateBasicTriangleReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "basic_triangle.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "basic_triangle.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "vertexMain",
                                            "Triangle vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Triangle vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto fragmentEntry = expectString(fragmentReflection->entry, "fragmentMain",
                                              "Triangle fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Triangle fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 2,
                           "Triangle vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            auto position =
                validateVertexInput(*vertexReflection, "POSITION", 0, 0, "float32", 1, 2);
            if (!position) {
                return std::unexpected{std::move(position.error())};
            }
            auto color = validateVertexInput(*vertexReflection, "COLOR", 0, 1, "float32", 1, 3);
            if (!color) {
                return std::unexpected{std::move(color.error())};
            }

            auto vertexResources =
                validateNoResourceBindings(*vertexReflection, "Triangle vertex shader");
            if (!vertexResources) {
                return std::unexpected{std::move(vertexResources.error())};
            }
            auto fragmentResources =
                validateNoResourceBindings(*fragmentReflection, "Triangle fragment shader");
            if (!fragmentResources) {
                return std::unexpected{std::move(fragmentResources.error())};
            }

            const std::array shaderReflections{*vertexReflection, *fragmentReflection};
            ShaderResourceSignature signature = shaderResourceSignature(shaderReflections);
            auto descriptorSignature =
                expectUint(signature.descriptorBindingCount, 0,
                           "Triangle pipeline layout descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(
                signature.pushConstantCount, 0, "Triangle pipeline layout push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            return signature;
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateDescriptorLayoutReflection(const std::filesystem::path& shaderDirectory) {
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "descriptor_layout.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto fragmentEntry = expectString(fragmentReflection->entry, "descriptorFragmentMain",
                                              "Descriptor layout fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Descriptor layout fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            const std::array shaderReflections{*fragmentReflection};
            ShaderResourceSignature signature = shaderResourceSignature(shaderReflections);
            auto descriptorSignature =
                expectUint(signature.descriptorBindingCount, 1,
                           "Descriptor layout smoke descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(
                signature.pushConstantCount, 0, "Descriptor layout smoke push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            const ShaderDescriptorBindingReflection& binding = signature.descriptorBindings.front();
            auto set = expectUint(binding.set, 0, "Descriptor layout smoke descriptor set");
            if (!set) {
                return std::unexpected{std::move(set.error())};
            }
            auto bindingIndex =
                expectUint(binding.binding, 0, "Descriptor layout smoke descriptor binding");
            if (!bindingIndex) {
                return std::unexpected{std::move(bindingIndex.error())};
            }
            auto count = expectUint(binding.count, 1, "Descriptor layout smoke descriptor count");
            if (!count) {
                return std::unexpected{std::move(count.error())};
            }
            auto kind = expectString(binding.kind, "constantBuffer",
                                     "Descriptor layout smoke descriptor kind");
            if (!kind) {
                return std::unexpected{std::move(kind.error())};
            }
            auto stage = expectString(binding.stageVisibility, "fragment",
                                      "Descriptor layout smoke descriptor stage");
            if (!stage) {
                return std::unexpected{std::move(stage.error())};
            }

            return signature;
        }

        void recordTransferClear(const VulkanFrameRecordContext& frame) {
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(frame.commandBuffer, frame.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &frame.clearColor, 1,
                                 &clearRange);
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

        void recordTriangleDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                                VkBuffer vertexBuffer, BasicDrawItem drawItem,
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
                .depthStencil = VkClearDepthStencilValue{
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
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &vertexBuffer, &vertexBufferOffset);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDraw(frame.commandBuffer, drawItem.vertexCount, drawItem.instanceCount,
                      drawItem.firstVertex, drawItem.firstInstance);
            vkCmdEndRendering(frame.commandBuffer);
        }

        [[nodiscard]] bool usesImage(std::span<const RenderGraphImageHandle> images,
                                     RenderGraphImageHandle image) {
            return std::ranges::any_of(images, [image](RenderGraphImageHandle used) {
                return used == image;
            });
        }

        [[nodiscard]] VkImageUsageFlags transientUsageFlags(
            const RenderGraphCompileResult& compiled, RenderGraphImageHandle image) {
            VkImageUsageFlags usage{};
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                if (usesImage(pass.transferWrites, image)) {
                    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

        [[nodiscard]] Result<void> prepareTransientResources(
            VkDevice device, VmaAllocator allocator, const RenderGraphCompileResult& compiled,
            std::vector<VulkanRenderGraphImageBinding>& bindings,
            std::vector<VulkanImage>& transientImages,
            std::vector<VulkanImageView>& transientImageViews) {
            transientImageViews.clear();
            transientImages.clear();

            for (const RenderGraphTransientImageAllocation& allocation :
                 compiled.transientImages) {
                const VkFormat format = vulkanFormat(allocation.format);
                const VkImageAspectFlags aspectMask =
                    basicRenderGraphImageAspect(allocation.format);
                const VkImageUsageFlags usage =
                    transientUsageFlags(compiled, allocation.image);

                auto image = VulkanImage::create(VulkanImageDesc{
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
                if (!image) {
                    return std::unexpected{std::move(image.error())};
                }

                auto imageView = VulkanImageView::create(VulkanImageViewDesc{
                    .device = device,
                    .image = image->handle(),
                    .format = image->format(),
                    .aspectMask = image->aspectMask(),
                });
                if (!imageView) {
                    return std::unexpected{std::move(imageView.error())};
                }

                bindings.push_back(VulkanRenderGraphImageBinding{
                    .image = allocation.image,
                    .vulkanImage = image->handle(),
                    .vulkanImageView = imageView->handle(),
                    .aspectMask = image->aspectMask(),
                });
                transientImages.push_back(std::move(*image));
                transientImageViews.push_back(std::move(*imageView));
            }

            return {};
        }

    } // namespace

    Result<void> validateBasicDescriptorLayoutSmoke(const BasicDescriptorLayoutSmokeDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot validate descriptor layout smoke without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot validate descriptor layout smoke without an allocator"}};
        }

        auto signature = validateDescriptorLayoutReflection(desc.shaderDirectory);
        if (!signature) {
            return std::unexpected{std::move(signature.error())};
        }

        auto resources = createPipelineLayoutResources(desc.device, *signature);
        if (!resources) {
            return std::unexpected{std::move(resources.error())};
        }

        constexpr std::array<std::uint32_t, 4> uniformData{1, 2, 3, 4};
        auto uniformBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(uniformData),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!uniformBuffer) {
            return std::unexpected{std::move(uniformBuffer.error())};
        }
        auto uploaded = uniformBuffer->upload(std::as_bytes(std::span{uniformData}));
        if (!uploaded) {
            return std::unexpected{std::move(uploaded.error())};
        }

        constexpr std::array poolSizes{
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = 1,
            },
        };
        auto descriptorPool = VulkanDescriptorPool::create(VulkanDescriptorPoolDesc{
            .device = desc.device,
            .maxSets = 1,
            .poolSizes = poolSizes,
        });
        if (!descriptorPool) {
            return std::unexpected{std::move(descriptorPool.error())};
        }

        if (resources->descriptorSetLayouts.empty()) {
            return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                         "Descriptor layout smoke produced no set layouts"}};
        }
        const std::array setLayouts{resources->descriptorSetLayouts.front().handle()};
        auto descriptorSets = descriptorPool->allocate(VulkanDescriptorSetAllocationDesc{
            .setLayouts = setLayouts,
        });
        if (!descriptorSets) {
            return std::unexpected{std::move(descriptorSets.error())};
        }
        if (descriptorSets->size() != 1 || descriptorSets->front() == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Descriptor layout smoke failed to allocate one descriptor set"}};
        }

        const std::array descriptorWrites{
            VulkanDescriptorBufferWrite{
                .descriptorSet = descriptorSets->front(),
                .binding = 0,
                .arrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .buffer = uniformBuffer->handle(),
                .offset = 0,
                .range = uniformBuffer->size(),
            },
        };
        updateVulkanDescriptorBuffers(desc.device, descriptorWrites);

        return {};
    }

    BasicTriangleRenderer::BasicTriangleRenderer(BasicTriangleRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicTriangleRenderer&
    BasicTriangleRenderer::operator=(BasicTriangleRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        transientImages_ = std::move(other.transientImages_);
        transientImageViews_ = std::move(other.transientImageViews_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        pipelineDepthFormat_ = std::exchange(other.pipelineDepthFormat_, VK_FORMAT_UNDEFINED);
        allocator_ = std::exchange(other.allocator_, nullptr);
        drawItem_ = std::exchange(other.drawItem_, basicTriangleDrawItem());
        return *this;
    }

    Result<BasicTriangleRenderer>
    BasicTriangleRenderer::create(const BasicTriangleRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create triangle renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                         "Cannot create triangle renderer without an allocator"}};
        }
        if (desc.drawItem.vertexCount == 0 || desc.drawItem.instanceCount == 0) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Triangle renderer draw item must draw something"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto reflection = validateBasicTriangleReflection(desc.shaderDirectory);
        if (!reflection) {
            return std::unexpected{std::move(reflection.error())};
        }

        auto vertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
            .device = desc.device,
            .code = *vertexCode,
        });
        if (!vertexShader) {
            return std::unexpected{std::move(vertexShader.error())};
        }
        auto fragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
            .device = desc.device,
            .code = *fragmentCode,
        });
        if (!fragmentShader) {
            return std::unexpected{std::move(fragmentShader.error())};
        }

        auto layoutResources = createPipelineLayoutResources(desc.device, *reflection);
        if (!layoutResources) {
            return std::unexpected{std::move(layoutResources.error())};
        }

        constexpr auto vertices = basicTriangleVertices();
        auto vertexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(vertices),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!vertexBuffer) {
            return std::unexpected{std::move(vertexBuffer.error())};
        }
        auto uploaded = vertexBuffer->upload(std::as_bytes(std::span{vertices}));
        if (!uploaded) {
            return std::unexpected{std::move(uploaded.error())};
        }

        BasicTriangleRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.descriptorSetLayouts_ = std::move(layoutResources->descriptorSetLayouts);
        renderer.pipelineLayout_ = std::move(layoutResources->pipelineLayout);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.drawItem_ = desc.drawItem;
        return renderer;
    }

    Result<void> BasicTriangleRenderer::ensurePipeline(VkFormat colorFormat,
                                                       VkFormat depthFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat &&
            pipelineDepthFormat_ == depthFormat) {
            return {};
        }

        const auto bindings = basicVertexInputBindings();
        const auto attributes = basicVertexInputAttributes();

        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
            .device = device_,
            .layout = pipelineLayout_.handle(),
            .vertexShader = vertexShader_.handle(),
            .fragmentShader = fragmentShader_.handle(),
            .vertexEntryPoint = "main",
            .fragmentEntryPoint = "main",
            .colorFormat = colorFormat,
            .depthFormat = depthFormat,
            .vertexBindings = bindings,
            .vertexAttributes = attributes,
        });
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        pipeline_ = std::move(*pipeline);
        pipelineFormat_ = colorFormat;
        pipelineDepthFormat_ = depthFormat;
        return {};
    }

    Result<VulkanFrameRecordResult>
    BasicTriangleRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        auto pipeline = ensurePipeline(frame.format);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const std::array bindings{basicBackbufferBinding(backbuffer, frame)};

        graph.addPass("ClearColor")
            .writeTransfer("target", backbuffer)
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                recordTransferClear(frame);
                return {};
            });

        graph.addPass("Triangle")
            .writeColor("target", backbuffer)
            .execute([&frame, &bindings, this](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                recordTriangleDraw(frame, pipeline_.handle(), vertexBuffer_.handle(), drawItem_);
                return {};
            });

        auto compiled = graph.compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }

        auto finalTransitions =
            recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
        if (!finalTransitions) {
            return std::unexpected{std::move(finalTransitions.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    Result<VulkanFrameRecordResult>
    BasicTriangleRenderer::recordFrameWithDepth(const VulkanFrameRecordContext& frame) {
        constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        auto pipeline = ensurePipeline(frame.format, kDepthFormat);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto depth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "DepthBuffer",
            .format = RenderGraphImageFormat::D32Sfloat,
            .extent = basicRenderGraphExtent(frame.extent),
        });

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(2);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));

        graph.addPass("ClearColor")
            .writeTransfer("target", backbuffer)
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                recordTransferClear(frame);
                return {};
            });

        graph.addPass("DepthTriangle")
            .writeColor("target", backbuffer)
            .writeDepth("depth", depth)
            .execute([&frame, &bindings, depth, this](
                         RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto depthBinding = findVulkanRenderGraphImage(depth, bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }

                recordTriangleDraw(frame, pipeline_.handle(), vertexBuffer_.handle(), drawItem_,
                                   depthBinding->vulkanImageView);
                return {};
            });

        auto compiled = graph.compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        auto prepared = prepareTransientResources(device_, allocator_, *compiled, bindings,
                                                  transientImages_, transientImageViews_);
        if (!prepared) {
            return std::unexpected{std::move(prepared.error())};
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }

        auto finalTransitions =
            recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
        if (!finalTransitions) {
            return std::unexpected{std::move(finalTransitions.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

} // namespace vke

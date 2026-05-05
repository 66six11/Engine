#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <numbers>
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

        [[nodiscard]] Error renderGraphError(std::string message) {
            return Error{ErrorDomain::RenderGraph, 0, std::move(message)};
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

        struct BasicFullscreenPipelineKey {
            std::string shaderAsset;
            std::string shaderPass;
            std::string textureBinding;
            std::string textureSlot;
        };

        struct BasicTransferClearParams {
            std::array<float, 4> color{};
        };

        struct BasicFullscreenParams {
            std::array<float, 4> tint{};
        };

        struct BasicDrawListParams {
            std::uint32_t drawCount{};
        };

        struct BasicMesh3DPushConstants {
            std::array<float, 4> mvpRow0{};
            std::array<float, 4> mvpRow1{};
            std::array<float, 4> mvpRow2{};
            std::array<float, 4> mvpRow3{};
        };

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

        [[nodiscard]] const ShaderDescriptorBindingReflection*
        findDescriptorBinding(const ShaderResourceSignature& signature, std::uint32_t set,
                              std::uint32_t binding) {
            for (const ShaderDescriptorBindingReflection& descriptor :
                 signature.descriptorBindings) {
                if (descriptor.set == set && descriptor.binding == binding) {
                    return &descriptor;
                }
            }

            return nullptr;
        }

        [[nodiscard]] Result<void>
        validateDescriptorBinding(const ShaderResourceSignature& signature, std::uint32_t set,
                                  std::uint32_t binding, std::string_view expectedKind,
                                  std::string_view context) {
            const ShaderDescriptorBindingReflection* descriptor =
                findDescriptorBinding(signature, set, binding);
            if (descriptor == nullptr) {
                return std::unexpected{
                    shaderError(std::string{context} + " missing descriptor binding")};
            }

            auto descriptorSet = expectUint(descriptor->set, set, std::string{context} + " set");
            if (!descriptorSet) {
                return std::unexpected{std::move(descriptorSet.error())};
            }
            auto bindingIndex =
                expectUint(descriptor->binding, binding, std::string{context} + " binding");
            if (!bindingIndex) {
                return std::unexpected{std::move(bindingIndex.error())};
            }
            auto count = expectUint(descriptor->count, 1, std::string{context} + " count");
            if (!count) {
                return std::unexpected{std::move(count.error())};
            }
            auto kind =
                expectString(descriptor->kind, expectedKind, std::string{context} + " kind");
            if (!kind) {
                return std::unexpected{std::move(kind.error())};
            }
            auto stage = expectString(descriptor->stageVisibility, "fragment",
                                      std::string{context} + " stage");
            if (!stage) {
                return std::unexpected{std::move(stage.error())};
            }

            return {};
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

        [[nodiscard]] Result<void>
        validateMesh3DReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "basic_mesh3d.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "basic_mesh3d.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "mesh3DVertexMain",
                                            "Mesh3D vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Mesh3D vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto fragmentEntry = expectString(fragmentReflection->entry, "mesh3DFragmentMain",
                                              "Mesh3D fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Mesh3D fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 2,
                           "Mesh3D vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            auto position =
                validateVertexInput(*vertexReflection, "POSITION", 0, 0, "float32", 1, 3);
            if (!position) {
                return std::unexpected{std::move(position.error())};
            }
            auto color = validateVertexInput(*vertexReflection, "COLOR", 0, 1, "float32", 1, 3);
            if (!color) {
                return std::unexpected{std::move(color.error())};
            }

            return {};
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
                expectUint(signature.descriptorBindingCount, 3,
                           "Descriptor layout smoke descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(
                signature.pushConstantCount, 0, "Descriptor layout smoke push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            auto constantBuffer = validateDescriptorBinding(
                signature, 0, 0, "constantBuffer", "Descriptor layout smoke constant buffer");
            if (!constantBuffer) {
                return std::unexpected{std::move(constantBuffer.error())};
            }
            auto texture = validateDescriptorBinding(signature, 0, 1, "texture",
                                                     "Descriptor layout smoke sampled image");
            if (!texture) {
                return std::unexpected{std::move(texture.error())};
            }
            auto sampler = validateDescriptorBinding(signature, 0, 2, "sampler",
                                                     "Descriptor layout smoke sampler");
            if (!sampler) {
                return std::unexpected{std::move(sampler.error())};
            }

            return signature;
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateFullscreenTextureReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "descriptor_layout.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentSignature = validateDescriptorLayoutReflection(shaderDirectory);
            if (!fragmentSignature) {
                return std::unexpected{std::move(fragmentSignature.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "descriptorVertexMain",
                                            "Fullscreen vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Fullscreen vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 0,
                           "Fullscreen vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            return *fragmentSignature;
        }

        [[nodiscard]] RenderGraphSchemaRegistry basicFullscreenSchemaRegistry() {
            RenderGraphSchemaRegistry schemas;
            schemas.registerSchema(RenderGraphPassSchema{
                .type = "builtin.transfer-clear",
                .paramsType = "builtin.transfer-clear.params",
                .resourceSlots =
                    {
                        RenderGraphResourceSlotSchema{
                            .name = "target",
                            .access = RenderGraphSlotAccess::TransferWrite,
                            .shaderStage = RenderGraphShaderStage::None,
                            .optional = false,
                        },
                    },
                .allowedCommands = {RenderGraphCommandKind::ClearColor},
            });
            schemas.registerSchema(RenderGraphPassSchema{
                .type = "builtin.raster-fullscreen",
                .paramsType = "builtin.raster-fullscreen.params",
                .resourceSlots =
                    {
                        RenderGraphResourceSlotSchema{
                            .name = "source",
                            .access = RenderGraphSlotAccess::ShaderRead,
                            .shaderStage = RenderGraphShaderStage::Fragment,
                            .optional = false,
                        },
                        RenderGraphResourceSlotSchema{
                            .name = "target",
                            .access = RenderGraphSlotAccess::ColorWrite,
                            .shaderStage = RenderGraphShaderStage::None,
                            .optional = false,
                        },
                    },
                .allowedCommands =
                    {
                        RenderGraphCommandKind::SetShader,
                        RenderGraphCommandKind::SetTexture,
                        RenderGraphCommandKind::SetVec4,
                        RenderGraphCommandKind::DrawFullscreenTriangle,
                    },
            });
            return schemas;
        }

        [[nodiscard]] RenderGraphSchemaRegistry basicDrawListSchemaRegistry() {
            RenderGraphSchemaRegistry schemas;
            schemas.registerSchema(RenderGraphPassSchema{
                .type = "builtin.transfer-clear",
                .paramsType = "builtin.transfer-clear.params",
                .resourceSlots =
                    {
                        RenderGraphResourceSlotSchema{
                            .name = "target",
                            .access = RenderGraphSlotAccess::TransferWrite,
                            .shaderStage = RenderGraphShaderStage::None,
                            .optional = false,
                        },
                    },
                .allowedCommands = {RenderGraphCommandKind::ClearColor},
            });
            schemas.registerSchema(RenderGraphPassSchema{
                .type = "builtin.raster-draw-list",
                .paramsType = "builtin.raster-draw-list.params",
                .resourceSlots =
                    {
                        RenderGraphResourceSlotSchema{
                            .name = "target",
                            .access = RenderGraphSlotAccess::ColorWrite,
                            .shaderStage = RenderGraphShaderStage::None,
                            .optional = false,
                        },
                        RenderGraphResourceSlotSchema{
                            .name = "depth",
                            .access = RenderGraphSlotAccess::DepthAttachmentWrite,
                            .shaderStage = RenderGraphShaderStage::None,
                            .optional = false,
                        },
                    },
                .allowedCommands = {},
            });
            return schemas;
        }

        void recordTransferClear(const VulkanFrameRecordContext& frame,
                                 VkClearColorValue clearColor) {
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(frame.commandBuffer, frame.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1,
                                 &clearRange);
        }

        void recordTransferClear(const VulkanFrameRecordContext& frame) {
            recordTransferClear(frame, frame.clearColor);
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
                              VkImageView depthImageView) {
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
            const BasicMesh3DPushConstants pushConstants =
                basicMesh3DPushConstants(frame.extent, basicMesh3DModelMatrix());

            vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               static_cast<std::uint32_t>(sizeof(pushConstants)), &pushConstants);
            constexpr VkDeviceSize vertexBufferOffset = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffers.vertex, &vertexBufferOffset);
            vkCmdBindIndexBuffer(frame.commandBuffer, buffers.index, 0, VK_INDEX_TYPE_UINT16);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDrawIndexed(
                frame.commandBuffer, basicIndexedCubeDrawItem().indexCount,
                basicIndexedCubeDrawItem().instanceCount, basicIndexedCubeDrawItem().firstIndex,
                basicIndexedCubeDrawItem().vertexOffset, basicIndexedCubeDrawItem().firstInstance);
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

        void recordFullscreenTextureDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                                         VkPipelineLayout pipelineLayout,
                                         VkDescriptorSet descriptorSet) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = frame.imageView;
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
                .extent = frame.extent,
            };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

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
            vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(frame.commandBuffer);
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
        prepareTransientResources(VkDevice device, VmaAllocator allocator,
                                  const RenderGraphCompileResult& compiled,
                                  std::vector<VulkanRenderGraphImageBinding>& bindings,
                                  std::vector<VulkanImage>& transientImages,
                                  std::vector<VulkanImageView>& transientImageViews) {
            transientImageViews.clear();
            transientImages.clear();

            for (const RenderGraphTransientImageAllocation& allocation : compiled.transientImages) {
                const VkFormat format = vulkanFormat(allocation.format);
                const VkImageAspectFlags aspectMask =
                    basicRenderGraphImageAspect(allocation.format);
                const VkImageUsageFlags usage = transientUsageFlags(compiled, allocation.image);

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
                    return std::unexpected{Error{
                        ErrorDomain::Vulkan, 0,
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
                        maxIndex =
                            std::max(maxIndex, static_cast<std::uint32_t>(indexValue));
                    }
                    const std::size_t maxVertex =
                        static_cast<std::size_t>(item.drawItem.vertexOffset) + maxIndex;
                    if (maxVertex >= vertices.size()) {
                        return std::unexpected{
                            Error{ErrorDomain::Vulkan, 0,
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

        auto sampledImage = VulkanImage::create(VulkanImageDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = VkExtent2D{.width = 1, .height = 1},
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        });
        if (!sampledImage) {
            return std::unexpected{std::move(sampledImage.error())};
        }
        auto sampledImageView = VulkanImageView::create(VulkanImageViewDesc{
            .device = desc.device,
            .image = sampledImage->handle(),
            .format = sampledImage->format(),
            .aspectMask = sampledImage->aspectMask(),
        });
        if (!sampledImageView) {
            return std::unexpected{std::move(sampledImageView.error())};
        }
        auto sampler = VulkanSampler::create(VulkanSamplerDesc{
            .device = desc.device,
        });
        if (!sampler) {
            return std::unexpected{std::move(sampler.error())};
        }

        constexpr std::array poolSizes{
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = 1,
            },
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .count = 1,
            },
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
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
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Descriptor layout smoke produced no set layouts"}};
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

        const std::array imageDescriptorWrites{
            VulkanDescriptorImageWrite{
                .descriptorSet = descriptorSets->front(),
                .binding = 1,
                .arrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .imageView = sampledImageView->handle(),
                .sampler = VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            VulkanDescriptorImageWrite{
                .descriptorSet = descriptorSets->front(),
                .binding = 2,
                .arrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .imageView = VK_NULL_HANDLE,
                .sampler = sampler->handle(),
                .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
        };
        updateVulkanDescriptorImages(desc.device, imageDescriptorWrites);

        return {};
    }

    BasicFullscreenTextureRenderer::BasicFullscreenTextureRenderer(
        BasicFullscreenTextureRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicFullscreenTextureRenderer&
    BasicFullscreenTextureRenderer::operator=(BasicFullscreenTextureRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        descriptorPool_ = std::move(other.descriptorPool_);
        descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
        uniformBuffer_ = std::move(other.uniformBuffer_);
        sampler_ = std::move(other.sampler_);
        transientImages_ = std::move(other.transientImages_);
        transientImageViews_ = std::move(other.transientImageViews_);
        return *this;
    }

    Result<BasicFullscreenTextureRenderer>
    BasicFullscreenTextureRenderer::create(const BasicFullscreenTextureRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot create fullscreen texture renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot create fullscreen texture renderer without an allocator"}};
        }

        auto signature = validateFullscreenTextureReflection(desc.shaderDirectory);
        if (!signature) {
            return std::unexpected{std::move(signature.error())};
        }
        auto resources = createPipelineLayoutResources(desc.device, *signature);
        if (!resources) {
            return std::unexpected{std::move(resources.error())};
        }
        if (resources->descriptorSetLayouts.empty()) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Fullscreen texture renderer produced no descriptor set layout"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "descriptor_layout.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "descriptor_layout.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
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

        constexpr std::array tint{1.0F, 1.0F, 1.0F, 1.0F};
        auto uniformBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(tint),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!uniformBuffer) {
            return std::unexpected{std::move(uniformBuffer.error())};
        }
        auto uploaded = uniformBuffer->upload(std::as_bytes(std::span{tint}));
        if (!uploaded) {
            return std::unexpected{std::move(uploaded.error())};
        }

        auto sampler = VulkanSampler::create(VulkanSamplerDesc{.device = desc.device});
        if (!sampler) {
            return std::unexpected{std::move(sampler.error())};
        }

        constexpr std::array poolSizes{
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = 1,
            },
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .count = 1,
            },
            VulkanDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
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
                      "Fullscreen texture renderer failed to allocate one descriptor set"}};
        }

        const std::array bufferWrites{
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
        updateVulkanDescriptorBuffers(desc.device, bufferWrites);

        const std::array samplerWrites{
            VulkanDescriptorImageWrite{
                .descriptorSet = descriptorSets->front(),
                .binding = 2,
                .arrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .imageView = VK_NULL_HANDLE,
                .sampler = sampler->handle(),
                .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
        };
        updateVulkanDescriptorImages(desc.device, samplerWrites);

        BasicFullscreenTextureRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.descriptorSetLayouts_ = std::move(resources->descriptorSetLayouts);
        renderer.pipelineLayout_ = std::move(resources->pipelineLayout);
        renderer.descriptorPool_ = std::move(*descriptorPool);
        renderer.descriptorSet_ = descriptorSets->front();
        renderer.uniformBuffer_ = std::move(*uniformBuffer);
        renderer.sampler_ = std::move(*sampler);
        return renderer;
    }

    Result<void> BasicFullscreenTextureRenderer::ensurePipeline(VkFormat colorFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat) {
            return {};
        }

        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
            .device = device_,
            .layout = pipelineLayout_.handle(),
            .vertexShader = vertexShader_.handle(),
            .fragmentShader = fragmentShader_.handle(),
            .vertexEntryPoint = "main",
            .fragmentEntryPoint = "main",
            .colorFormat = colorFormat,
            .vertexBindings = {},
            .vertexAttributes = {},
        });
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        pipeline_ = std::move(*pipeline);
        pipelineFormat_ = colorFormat;
        return {};
    }

    Result<void>
    BasicFullscreenTextureRenderer::updateSourceDescriptor(VkImageView sourceImageView) {
        if (descriptorSet_ == VK_NULL_HANDLE || sourceImageView == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot update fullscreen source descriptor from incomplete inputs"}};
        }

        const std::array imageWrites{
            VulkanDescriptorImageWrite{
                .descriptorSet = descriptorSet_,
                .binding = 1,
                .arrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .imageView = sourceImageView,
                .sampler = VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        };
        updateVulkanDescriptorImages(device_, imageWrites);
        return {};
    }

    Result<VulkanFrameRecordResult>
    BasicFullscreenTextureRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        auto pipeline = ensurePipeline(frame.format);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        transientImageViews_.clear();
        transientImages_.clear();

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto source = graph.createTransientImage(RenderGraphImageDesc{
            .name = "FullscreenSource",
            .format = basicRenderGraphImageFormat(frame.format),
            .extent = basicRenderGraphExtent(frame.extent),
        });

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(2);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));

        constexpr BasicTransferClearParams kClearParams{
            .color = {0.18F, 0.36F, 0.95F, 1.0F},
        };
        constexpr BasicFullscreenParams kFullscreenParams{
            .tint = {1.0F, 1.0F, 1.0F, 1.0F},
        };

        graph.addPass("ClearFullscreenSource", "builtin.transfer-clear")
            .setParams("builtin.transfer-clear.params", kClearParams)
            .writeTransfer("target", source)
            .recordCommands([kClearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", kClearParams.color);
            })
            .execute([&frame, &bindings, source](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto clearParams = readPassParams<BasicTransferClearParams>(
                    pass, "builtin.transfer-clear.params", "Fullscreen source clear pass");
                if (!clearParams) {
                    return std::unexpected{std::move(clearParams.error())};
                }

                auto sourceBinding = findVulkanRenderGraphImage(source, bindings);
                if (!sourceBinding) {
                    return std::unexpected{std::move(sourceBinding.error())};
                }

                const VkClearColorValue sourceColor{{
                    clearParams->color[0],
                    clearParams->color[1],
                    clearParams->color[2],
                    clearParams->color[3],
                }};
                VkImageSubresourceRange clearRange{};
                clearRange.aspectMask = sourceBinding->aspectMask;
                clearRange.baseMipLevel = 0;
                clearRange.levelCount = 1;
                clearRange.baseArrayLayer = 0;
                clearRange.layerCount = 1;
                vkCmdClearColorImage(frame.commandBuffer, sourceBinding->vulkanImage,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &sourceColor, 1,
                                     &clearRange);
                return {};
            });

        graph.addPass("FullscreenTexture", "builtin.raster-fullscreen")
            .setParams("builtin.raster-fullscreen.params", kFullscreenParams)
            .readTexture("source", source, RenderGraphShaderStage::Fragment)
            .writeColor("target", backbuffer)
            .recordCommands([kFullscreenParams](RenderGraphCommandList& commands) {
                commands.setShader("Hidden/DescriptorLayout", "Fullscreen")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", kFullscreenParams.tint)
                    .drawFullscreenTriangle();
            })
            .execute([&frame, &bindings, source,
                      this](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto fullscreenParams = readPassParams<BasicFullscreenParams>(
                    pass, "builtin.raster-fullscreen.params", "Fullscreen texture pass");
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
                        renderGraphError("Fullscreen pipeline key references an unknown texture "
                                         "slot")};
                }

                auto sourceBinding = findVulkanRenderGraphImage(source, bindings);
                if (!sourceBinding) {
                    return std::unexpected{std::move(sourceBinding.error())};
                }
                auto descriptor = updateSourceDescriptor(sourceBinding->vulkanImageView);
                if (!descriptor) {
                    return std::unexpected{std::move(descriptor.error())};
                }

                recordFullscreenTextureDraw(frame, pipeline_.handle(), pipelineLayout_.handle(),
                                            descriptorSet_);
                return {};
            });

        const RenderGraphSchemaRegistry schemas = basicFullscreenSchemaRegistry();
        auto compiled = graph.compile(schemas);
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
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
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
        indexBuffer_ = std::move(other.indexBuffer_);
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
        const BasicDrawItem drawItem = desc.meshKind == BasicMeshKind::IndexedQuad
                                           ? basicIndexedQuadDrawItem()
                                           : desc.drawItem;
        if ((drawItem.vertexCount == 0 && drawItem.indexCount == 0) ||
            drawItem.instanceCount == 0) {
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

        constexpr auto triangleVertices = basicTriangleVertices();
        constexpr auto quadVertices = basicQuadVertices();
        const std::span<const BasicVertex> vertices =
            desc.meshKind == BasicMeshKind::IndexedQuad
                ? std::span<const BasicVertex>{quadVertices}
                : std::span<const BasicVertex>{triangleVertices};
        auto vertexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = vertices.size_bytes(),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!vertexBuffer) {
            return std::unexpected{std::move(vertexBuffer.error())};
        }
        auto uploaded = vertexBuffer->upload(std::as_bytes(vertices));
        if (!uploaded) {
            return std::unexpected{std::move(uploaded.error())};
        }

        VulkanBuffer indexBuffer;
        if (drawItem.indexCount > 0) {
            constexpr auto indices = basicQuadIndices();
            auto createdIndexBuffer = VulkanBuffer::create(VulkanBufferDesc{
                .device = desc.device,
                .allocator = desc.allocator,
                .size = sizeof(indices),
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
            });
            if (!createdIndexBuffer) {
                return std::unexpected{std::move(createdIndexBuffer.error())};
            }
            auto uploadedIndices = createdIndexBuffer->upload(std::as_bytes(std::span{indices}));
            if (!uploadedIndices) {
                return std::unexpected{std::move(uploadedIndices.error())};
            }
            indexBuffer = std::move(*createdIndexBuffer);
        }

        BasicTriangleRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.descriptorSetLayouts_ = std::move(layoutResources->descriptorSetLayouts);
        renderer.pipelineLayout_ = std::move(layoutResources->pipelineLayout);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.indexBuffer_ = std::move(indexBuffer);
        renderer.drawItem_ = drawItem;
        return renderer;
    }

    Result<void> BasicTriangleRenderer::ensurePipeline(VkFormat colorFormat, VkFormat depthFormat) {
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
                recordTriangleDraw(frame, pipeline_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   drawItem_);
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
            .execute([&frame, &bindings, depth, this](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto depthBinding = findVulkanRenderGraphImage(depth, bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }

                recordTriangleDraw(frame, pipeline_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   drawItem_, depthBinding->vulkanImageView);
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

    BasicMesh3DRenderer::BasicMesh3DRenderer(BasicMesh3DRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicMesh3DRenderer& BasicMesh3DRenderer::operator=(BasicMesh3DRenderer&& other) noexcept {
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
        indexBuffer_ = std::move(other.indexBuffer_);
        transientImages_ = std::move(other.transientImages_);
        transientImageViews_ = std::move(other.transientImageViews_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        pipelineDepthFormat_ = std::exchange(other.pipelineDepthFormat_, VK_FORMAT_UNDEFINED);
        allocator_ = std::exchange(other.allocator_, nullptr);
        return *this;
    }

    Result<BasicMesh3DRenderer> BasicMesh3DRenderer::create(const BasicMesh3DRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create mesh 3D renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                         "Cannot create mesh 3D renderer without an allocator"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto reflectionValidated = validateMesh3DReflection(desc.shaderDirectory);
        if (!reflectionValidated) {
            return std::unexpected{std::move(reflectionValidated.error())};
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

        constexpr std::array pushConstantRanges{
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<std::uint32_t>(sizeof(BasicMesh3DPushConstants)),
            },
        };
        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = desc.device,
            .setLayouts = {},
            .pushConstantRanges = pushConstantRanges,
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }

        constexpr auto vertices = basicCubeVertices();
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
        auto uploadedVertices = vertexBuffer->upload(std::as_bytes(std::span{vertices}));
        if (!uploadedVertices) {
            return std::unexpected{std::move(uploadedVertices.error())};
        }

        constexpr auto indices = basicCubeIndices();
        auto indexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(indices),
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!indexBuffer) {
            return std::unexpected{std::move(indexBuffer.error())};
        }
        auto uploadedIndices = indexBuffer->upload(std::as_bytes(std::span{indices}));
        if (!uploadedIndices) {
            return std::unexpected{std::move(uploadedIndices.error())};
        }

        BasicMesh3DRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.pipelineLayout_ = std::move(*pipelineLayout);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.indexBuffer_ = std::move(*indexBuffer);
        return renderer;
    }

    Result<void> BasicMesh3DRenderer::ensurePipeline(VkFormat colorFormat, VkFormat depthFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat &&
            pipelineDepthFormat_ == depthFormat) {
            return {};
        }

        const auto bindings = basicVertex3DInputBindings();
        const auto attributes = basicVertex3DInputAttributes();

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
    BasicMesh3DRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        auto pipeline = ensurePipeline(frame.format, kDepthFormat);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto depth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "Mesh3DDepthBuffer",
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

        graph.addPass("Mesh3D")
            .writeColor("target", backbuffer)
            .writeDepth("depth", depth)
            .execute([&frame, &bindings, depth, this](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto depthBinding = findVulkanRenderGraphImage(depth, bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }

                recordMesh3DDraw(frame, pipeline_.handle(), pipelineLayout_.handle(),
                                 BasicDrawBuffers{
                                     .vertex = vertexBuffer_.handle(),
                                     .index = indexBuffer_.handle(),
                                 },
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

    BasicDrawListRenderer::BasicDrawListRenderer(BasicDrawListRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicDrawListRenderer&
    BasicDrawListRenderer::operator=(BasicDrawListRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        indexBuffer_ = std::move(other.indexBuffer_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        pipelineDepthFormat_ = std::exchange(other.pipelineDepthFormat_, VK_FORMAT_UNDEFINED);
        drawItems_ = std::move(other.drawItems_);
        transientImages_ = std::move(other.transientImages_);
        transientImageViews_ = std::move(other.transientImageViews_);
        allocator_ = std::exchange(other.allocator_, nullptr);
        return *this;
    }

    Result<BasicDrawListRenderer> BasicDrawListRenderer::create(
        const BasicDrawListRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create draw list renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{Error{
                ErrorDomain::Vulkan, 0, "Cannot create draw list renderer without an allocator"}};
        }

        constexpr auto defaultDrawItems = basicDrawListSmokeItems();
        const std::span<const BasicDrawListItem> drawItems =
            desc.drawItems.empty()
                ? std::span<const BasicDrawListItem>{defaultDrawItems.data(),
                                                     defaultDrawItems.size()}
                : desc.drawItems;
        auto drawListValidated = validateBasicDrawListItems(drawItems);
        if (!drawListValidated) {
            return std::unexpected{std::move(drawListValidated.error())};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto reflectionValidated = validateMesh3DReflection(desc.shaderDirectory);
        if (!reflectionValidated) {
            return std::unexpected{std::move(reflectionValidated.error())};
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

        constexpr std::array pushConstantRanges{
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<std::uint32_t>(sizeof(BasicMesh3DPushConstants)),
            },
        };
        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = desc.device,
            .setLayouts = {},
            .pushConstantRanges = pushConstantRanges,
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }

        constexpr auto vertices = basicCubeVertices();
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
        auto uploadedVertices = vertexBuffer->upload(std::as_bytes(std::span{vertices}));
        if (!uploadedVertices) {
            return std::unexpected{std::move(uploadedVertices.error())};
        }

        constexpr auto indices = basicCubeIndices();
        auto indexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(indices),
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!indexBuffer) {
            return std::unexpected{std::move(indexBuffer.error())};
        }
        auto uploadedIndices = indexBuffer->upload(std::as_bytes(std::span{indices}));
        if (!uploadedIndices) {
            return std::unexpected{std::move(uploadedIndices.error())};
        }

        BasicDrawListRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.pipelineLayout_ = std::move(*pipelineLayout);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.indexBuffer_ = std::move(*indexBuffer);
        renderer.drawItems_.assign(drawItems.begin(), drawItems.end());
        return renderer;
    }

    Result<void> BasicDrawListRenderer::ensurePipeline(VkFormat colorFormat,
                                                       VkFormat depthFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat &&
            pipelineDepthFormat_ == depthFormat) {
            return {};
        }

        const auto bindings = basicVertex3DInputBindings();
        const auto attributes = basicVertex3DInputAttributes();

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
    BasicDrawListRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        auto pipeline = ensurePipeline(frame.format, kDepthFormat);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto depth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "DrawListDepthBuffer",
            .format = RenderGraphImageFormat::D32Sfloat,
            .extent = basicRenderGraphExtent(frame.extent),
        });
        const BasicDrawListParams drawListParams{
            .drawCount = static_cast<std::uint32_t>(drawItems_.size()),
        };
        const BasicTransferClearParams clearParams =
            basicTransferClearParams(frame.clearColor);

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(2);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));

        graph.addPass("ClearColor", "builtin.transfer-clear")
            .setParams("builtin.transfer-clear.params", clearParams)
            .writeTransfer("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto clearParams = readPassParams<BasicTransferClearParams>(
                    pass, "builtin.transfer-clear.params", "Draw list clear pass");
                if (!clearParams) {
                    return std::unexpected{std::move(clearParams.error())};
                }
                const VkClearColorValue clearColor{{
                    clearParams->color[0],
                    clearParams->color[1],
                    clearParams->color[2],
                    clearParams->color[3],
                }};
                recordTransferClear(frame, clearColor);
                return {};
            });

        graph.addPass("DrawList", "builtin.raster-draw-list")
            .setParams("builtin.raster-draw-list.params", drawListParams)
            .writeColor("target", backbuffer)
            .writeDepth("depth", depth)
            .execute([&frame, &bindings, depth, this](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto drawListParams = readPassParams<BasicDrawListParams>(
                    pass, "builtin.raster-draw-list.params", "Draw list pass");
                if (!drawListParams) {
                    return std::unexpected{std::move(drawListParams.error())};
                }
                if (static_cast<std::size_t>(drawListParams->drawCount) != drawItems_.size()) {
                    return std::unexpected{renderGraphError(
                        "Draw list pass params draw count does not match renderer draw list")};
                }

                auto depthBinding = findVulkanRenderGraphImage(depth, bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }

                recordDrawListDraw(frame, pipeline_.handle(), pipelineLayout_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   depthBinding->vulkanImageView, drawItems_);
                return {};
            });

        const RenderGraphSchemaRegistry schemas = basicDrawListSchemaRegistry();
        auto compiled = graph.compile(schemas);
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

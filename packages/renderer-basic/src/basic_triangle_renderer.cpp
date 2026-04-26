#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"

#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/renderer_basic_vulkan/triangle_frame.hpp"

namespace vke {
    namespace {

        [[nodiscard]] Error shaderError(std::string message) {
            return Error{ErrorDomain::Shader, 0, std::move(message)};
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>> readSpirvFile(
            const std::filesystem::path& path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file) {
                return std::unexpected{shaderError("Failed to open SPIR-V file: " +
                                                   path.string())};
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 ||
                size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
                return std::unexpected{
                    shaderError("SPIR-V file size is invalid: " + path.string())};
            }

            std::vector<char> bytes(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(bytes.data(), size)) {
                return std::unexpected{
                    shaderError("Failed to read SPIR-V file: " + path.string())};
            }

            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

    } // namespace

    BasicTriangleRenderer::BasicTriangleRenderer(BasicTriangleRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicTriangleRenderer& BasicTriangleRenderer::operator=(
        BasicTriangleRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        return *this;
    }

    Result<BasicTriangleRenderer> BasicTriangleRenderer::create(
        const BasicTriangleRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create triangle renderer without a device"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.frag.spv");
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

        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = desc.device,
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }

        BasicTriangleRenderer renderer;
        renderer.device_ = desc.device;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.pipelineLayout_ = std::move(*pipelineLayout);
        return renderer;
    }

    Result<void> BasicTriangleRenderer::ensurePipeline(VkFormat colorFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat) {
            return {};
        }

        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
            .device = device_,
            .layout = pipelineLayout_.handle(),
            .vertexShader = vertexShader_.handle(),
            .fragmentShader = fragmentShader_.handle(),
            .colorFormat = colorFormat,
        });
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        pipeline_ = std::move(*pipeline);
        pipelineFormat_ = colorFormat;
        return {};
    }

    Result<VulkanFrameRecordResult> BasicTriangleRenderer::recordFrame(
        const VulkanFrameRecordContext& frame) {
        auto pipeline = ensurePipeline(frame.format);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        return recordBasicTriangleFrame(frame, pipeline_.handle());
    }

} // namespace vke

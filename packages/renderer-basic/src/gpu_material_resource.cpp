#include "asharia/renderer_basic_vulkan/gpu_material_resource.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace asharia {
    namespace {
        Error materialError(BasicGpuMaterialError code, const std::string& message) {
            return {ErrorDomain::Material, static_cast<int>(code), "GPU material: " + message};
        }

        bool validCode(std::span<const std::uint32_t> code) {
            return code.size() >= 5 && code.size() <= 1024ULL * 1024ULL && code[0] == 0x07230203U;
        }

        bool validVertex(const ShaderReflection& reflection) {
            if (reflection.stage != "vertex" || reflection.target != "spirv" ||
                reflection.entry != "mesh3DVertexMain" || reflection.vertexInputs.size() != 2 ||
                reflection.descriptorBindings.size() != 1 || reflection.pushConstants.size() != 1) {
                return false;
            }
            for (std::uint32_t i = 0; i < 2; ++i) {
                const auto& input = reflection.vertexInputs[i];
                if (input.location != i || input.scalarType != "float32" || input.rowCount != 1 ||
                    input.columnCount != 3 || input.semanticIndex != 0 ||
                    input.semantic != (i == 0 ? "POSITION" : "COLOR")) {
                    return false;
                }
            }
            const auto& push = reflection.descriptorBindings.front();
            return push.name == "gMesh3D" && push.category == "pushConstantBuffer" &&
                   push.parameterBlock && push.parameterBlock->size == 64 &&
                   reflection.pushConstants.front().name == "gMesh3D";
        }

        bool validLayout(const ShaderParameterBlockReflection& layout) {
            // Use the portable uniform-buffer range floor until device-specific limits are passed.
            if (layout.size == 0 || layout.size > 16384 || layout.members.empty() ||
                layout.members.size() > 256) {
                return false;
            }
            std::vector<bool> occupied(layout.size);
            std::vector<std::string_view> names;
            for (const auto& member : layout.members) {
                if (member.name.empty() || member.componentCount == 0 ||
                    member.componentCount > 4 || member.size != member.componentCount * 4 ||
                    member.offset % 4 != 0 || member.offset > layout.size ||
                    member.size > layout.size - member.offset ||
                    (member.scalarType != "float32" && member.scalarType != "int32" &&
                     member.scalarType != "uint32" && member.scalarType != "bool") ||
                    std::ranges::find(names, member.name) != names.end()) {
                    return false;
                }
                names.push_back(member.name);
                for (std::uint32_t i = member.offset; i < member.offset + member.size; ++i) {
                    if (occupied[i]) {
                        return false;
                    }
                    occupied[i] = true;
                }
            }
            return true;
        }
    } // namespace

    Result<std::shared_ptr<const BasicGpuMaterialProgram>>
    BasicGpuMaterialProgram::create(const BasicGpuMaterialProgramDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.colorFormat == VK_FORMAT_UNDEFINED ||
            desc.depthFormat == VK_FORMAT_UNDEFINED || !validCode(desc.vertexCode) ||
            !validCode(desc.fragmentCode) || desc.vertexReflection == nullptr ||
            desc.fragmentReflection == nullptr) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::InvalidInput,
                              "missing device, formats or validated shader input")};
        }
        const auto& fragment = *desc.fragmentReflection;
        if (!validVertex(*desc.vertexReflection) || fragment.stage != "fragment" ||
            fragment.target != "spirv" || fragment.descriptorBindings.size() != 1 ||
            !fragment.pushConstants.empty()) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::IncompatibleLayout,
                              "expected mesh vertex ABI and one fragment buffer")};
        }
        const auto& binding = fragment.descriptorBindings.front();
        if (binding.set != 1 || binding.binding != 0 || binding.kind != "constantBuffer" ||
            binding.count != 1 || !binding.parameterBlock ||
            !validLayout(*binding.parameterBlock)) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::IncompatibleLayout,
                              "expected numeric material block at set 1 binding 0")};
        }
        auto result = std::make_shared<BasicGpuMaterialProgram>();
        result->device_ = desc.device;
        result->colorFormat_ = desc.colorFormat;
        result->depthFormat_ = desc.depthFormat;
        result->binding_ = binding;
        auto vertex = VulkanShaderModule::create({.device = desc.device, .code = desc.vertexCode});
        if (!vertex) {
            return std::unexpected{std::move(vertex.error())};
        }
        result->vertex_ = std::move(*vertex);
        auto fragmentModule =
            VulkanShaderModule::create({.device = desc.device, .code = desc.fragmentCode});
        if (!fragmentModule) {
            return std::unexpected{std::move(fragmentModule.error())};
        }
        result->fragment_ = std::move(*fragmentModule);
        auto empty = VulkanDescriptorSetLayout::create({.device = desc.device, .bindings = {}});
        if (!empty) {
            return std::unexpected{std::move(empty.error())};
        }
        result->emptyLayout_ = std::move(*empty);
        const std::array bindings{
            VkDescriptorSetLayoutBinding{.binding = 0,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .pImmutableSamplers = nullptr}};
        auto material =
            VulkanDescriptorSetLayout::create({.device = desc.device, .bindings = bindings});
        if (!material) {
            return std::unexpected{std::move(material.error())};
        }
        result->materialLayout_ = std::move(*material);
        const std::array layouts{result->emptyLayout_.handle(), result->materialLayout_.handle()};
        const std::array pushes{
            VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = 64}};
        auto layout = VulkanPipelineLayout::create(
            {.device = desc.device, .setLayouts = layouts, .pushConstantRanges = pushes});
        if (!layout) {
            return std::unexpected{std::move(layout.error())};
        }
        result->layout_ = std::move(*layout);
        const std::array vertexBindings{
            VkVertexInputBindingDescription{.binding = 0,
                                            .stride = sizeof(BasicVertex3D),
                                            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};
        const std::array attributes{
            VkVertexInputAttributeDescription{.location = 0,
                                              .binding = 0,
                                              .format = VK_FORMAT_R32G32B32_SFLOAT,
                                              .offset = offsetof(BasicVertex3D, position)},
            VkVertexInputAttributeDescription{.location = 1,
                                              .binding = 0,
                                              .format = VK_FORMAT_R32G32B32_SFLOAT,
                                              .offset = offsetof(BasicVertex3D, color)}};
        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(
            {.device = desc.device,
             .layout = result->layout_.handle(),
             .vertexShader = result->vertex_.handle(),
             .fragmentShader = result->fragment_.handle(),
             .colorFormat = desc.colorFormat,
             .depthFormat = desc.depthFormat,
             .vertexBindings = vertexBindings,
             .vertexAttributes = attributes,
             .deviceCapabilities = {}});
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }
        result->pipeline_ = std::move(*pipeline);
        return result;
    }

    Result<BasicGpuMaterialOwner> BasicGpuMaterialOwner::create(BasicGpuMaterialOwnerDesc desc) {
        if (desc.allocator == nullptr || !desc.program || !desc.key ||
            desc.program->pipeline_.handle() == VK_NULL_HANDLE ||
            desc.key == kBasicDefaultUnlitMaterialResourceKey || desc.maxResidentVersions == 0 ||
            desc.maxResidentVersions > 64) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::InvalidInput, "invalid owner descriptor")};
        }
        BasicGpuMaterialOwner owner;
        owner.desc_ = std::move(desc);
        owner.thread_ = std::this_thread::get_id();
        return owner;
    }

    std::size_t BasicGpuMaterialOwner::residentVersions() const {
        return static_cast<std::size_t>(
            std::ranges::count_if(residents_, [](const auto& weak) { return !weak.expired(); }));
    }

    VoidResult BasicGpuMaterialOwner::update(std::uint64_t revision,
                                             const ShaderParameterBlockReflection& layout,
                                             std::span<const std::byte> parameters) {
        if (thread_ != std::this_thread::get_id()) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::WrongThread, "update on wrong thread")};
        }
        if (!desc_.program) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::InvalidInput, "moved-from owner")};
        }
        if (revision == 0 || revision <= lastRevision_) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::StaleRevision, "revision must increase")};
        }
        if (layout != *desc_.program->binding_.parameterBlock || parameters.size() != layout.size) {
            return std::unexpected{materialError(BasicGpuMaterialError::IncompatibleLayout,
                                                 "packed layout/size mismatch")};
        }
        std::erase_if(residents_, [](const auto& weak) { return weak.expired(); });
        if (residents_.size() >= desc_.maxResidentVersions) {
            return std::unexpected{materialError(BasicGpuMaterialError::BudgetExceeded,
                                                 "resident version budget exhausted")};
        }
        auto candidate = std::make_shared<BasicGpuMaterial>();
        candidate->program_ = desc_.program;
        candidate->key_ = desc_.key;
        candidate->revision_ = revision;
        auto buffer = VulkanBuffer::create({.device = desc_.program->device_,
                                            .allocator = desc_.allocator,
                                            .size = parameters.size(),
                                            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                            .memoryUsage = VulkanBufferMemoryUsage::HostUpload});
        if (!buffer) {
            return std::unexpected{std::move(buffer.error())};
        }
        if (auto uploaded = buffer->upload(parameters); !uploaded) {
            return uploaded;
        }
        candidate->parameters_ = std::move(*buffer);
        const std::array poolSizes{
            VulkanDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .count = 1}};
        auto allocator = VulkanDescriptorAllocator::create(
            {.device = desc_.program->device_, .maxSets = 1, .poolSizes = poolSizes});
        if (!allocator) {
            return std::unexpected{std::move(allocator.error())};
        }
        candidate->descriptors_ = std::move(*allocator);
        const std::array layouts{desc_.program->materialLayout_.handle()};
        auto sets = candidate->descriptors_.allocate({.setLayouts = layouts});
        if (!sets) {
            return std::unexpected{std::move(sets.error())};
        }
        candidate->descriptor_ = sets->front();
        const std::array writes{
            VulkanDescriptorBufferWrite{.descriptorSet = candidate->descriptor_,
                                        .binding = 0,
                                        .buffer = candidate->parameters_.handle(),
                                        .range = parameters.size()}};
        updateVulkanDescriptorBuffers(desc_.program->device_, writes);
        residents_.push_back(candidate);
        active_ = std::move(candidate);
        lastRevision_ = revision;
        return {};
    }

    Result<std::shared_ptr<const BasicGpuMaterial>> BasicGpuMaterialOwner::acquire() const {
        if (thread_ != std::this_thread::get_id()) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::WrongThread, "acquire on wrong thread")};
        }
        if (!active_) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::InvalidInput, "no published material")};
        }
        return active_;
    }

    VoidResult BasicGpuMaterialOwner::clear() {
        if (thread_ != std::this_thread::get_id()) {
            return std::unexpected{
                materialError(BasicGpuMaterialError::WrongThread, "clear on wrong thread")};
        }
        active_.reset();
        return {};
    }
} // namespace asharia

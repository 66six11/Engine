#include "asharia/rendergraph/render_graph.hpp"

#include <utility>

namespace asharia {

    RenderGraphCommandList& RenderGraphCommandList::setShader(std::string shaderAsset,
                                                              std::string shaderPass) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetShader,
            .name = std::move(shaderAsset),
            .secondaryName = std::move(shaderPass),
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setTexture(std::string bindingName,
                                                               std::string slotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetTexture,
            .name = std::move(bindingName),
            .secondaryName = std::move(slotName),
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setFloat(std::string bindingName, float value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetFloat,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = {value, 0.0F, 0.0F, 0.0F},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setInt(std::string bindingName, int value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetInt,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = {},
            .intValue = value,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setVec4(std::string bindingName,
                                                            std::array<float, 4> value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetVec4,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = value,
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::drawFullscreenTriangle() {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::DrawFullscreenTriangle,
            .name = {},
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::clearColor(std::string slotName,
                                                               std::array<float, 4> color) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::ClearColor,
            .name = std::move(slotName),
            .secondaryName = {},
            .floatValues = color,
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::copyImage(std::string sourceSlotName,
                                                              std::string targetSlotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::CopyImage,
            .name = std::move(sourceSlotName),
            .secondaryName = std::move(targetSlotName),
            .floatValues = {},
            .intValue = 0,
            .uintValues = {},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::dispatch(std::uint32_t groupCountX,
                                                             std::uint32_t groupCountY,
                                                             std::uint32_t groupCountZ) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::Dispatch,
            .name = {},
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
            .uintValues = {groupCountX, groupCountY, groupCountZ},
        });
        return *this;
    }

    std::span<const RenderGraphCommand> RenderGraphCommandList::commands() const {
        return commands_;
    }

    std::vector<RenderGraphCommand> RenderGraphCommandList::takeCommands() && {
        return std::move(commands_);
    }

    RenderGraphSchemaRegistry&
    RenderGraphSchemaRegistry::registerSchema(RenderGraphPassSchema schema) {
        for (RenderGraphPassSchema& registered : schemas_) {
            if (registered.type == schema.type) {
                registered = std::move(schema);
                return *this;
            }
        }

        schemas_.push_back(std::move(schema));
        return *this;
    }

    const RenderGraphPassSchema* RenderGraphSchemaRegistry::find(std::string_view type) const {
        for (const RenderGraphPassSchema& schema : schemas_) {
            if (schema.type == type) {
                return &schema;
            }
        }

        return nullptr;
    }

    RenderGraphExecutorRegistry&
    RenderGraphExecutorRegistry::registerExecutor(std::string type,
                                                  RenderGraphPassCallback callback) {
        for (Executor& executor : executors_) {
            if (executor.type == type) {
                executor.callback = std::move(callback);
                return *this;
            }
        }

        executors_.push_back(Executor{
            .type = std::move(type),
            .callback = std::move(callback),
        });
        return *this;
    }

    const RenderGraphPassCallback* RenderGraphExecutorRegistry::find(std::string_view type) const {
        for (const Executor& executor : executors_) {
            if (executor.type == type) {
                return &executor.callback;
            }
        }

        return nullptr;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeColor(RenderGraphImageHandle image) {
        return writeColor("target", image);
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeColor(std::string slotName,
                                                                   RenderGraphImageHandle image) {
        graph_->passes_[passIndex_].colorWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readTexture(std::string slotName, RenderGraphImageHandle image,
                                          RenderGraphShaderStage shaderStage) {
        graph_->passes_[passIndex_].shaderReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readDepth(std::string slotName,
                                                                  RenderGraphImageHandle image) {
        graph_->passes_[passIndex_].depthReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeDepth(std::string slotName,
                                                                   RenderGraphImageHandle image) {
        graph_->passes_[passIndex_].depthWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readDepthTexture(std::string slotName, RenderGraphImageHandle image,
                                               RenderGraphShaderStage shaderStage) {
        graph_->passes_[passIndex_].depthSampledReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTransfer(std::string slotName,
                                                                     RenderGraphImageHandle image) {
        graph_->passes_[passIndex_].transferReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTransfer(RenderGraphImageHandle image) {
        return readTransfer("source", image);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeTransfer(RenderGraphImageHandle image) {
        return writeTransfer("target", image);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeTransfer(std::string slotName, RenderGraphImageHandle image) {
        graph_->passes_[passIndex_].transferWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readBuffer(std::string slotName, RenderGraphBufferHandle buffer,
                                         RenderGraphShaderStage shaderStage) {
        graph_->passes_[passIndex_].bufferReadSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readTransferBuffer(std::string slotName,
                                                 RenderGraphBufferHandle buffer) {
        graph_->passes_[passIndex_].bufferTransferReadSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeBuffer(RenderGraphBufferHandle buffer) {
        return writeBuffer("target", buffer);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeBuffer(std::string slotName, RenderGraphBufferHandle buffer) {
        graph_->passes_[passIndex_].bufferWriteSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readWriteStorageBuffer(
        std::string slotName, RenderGraphBufferHandle buffer, RenderGraphShaderStage shaderStage) {
        graph_->passes_[passIndex_].bufferStorageReadWriteSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    std::string_view RenderGraph::PassBuilder::name() const {
        return graph_->passes_[passIndex_].name;
    }

    std::string_view RenderGraph::PassBuilder::type() const {
        return graph_->passes_[passIndex_].type;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::allowCulling(bool allow) {
        graph_->passes_[passIndex_].allowCulling = allow;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::hasSideEffects(bool hasSideEffects) {
        graph_->passes_[passIndex_].hasSideEffects = hasSideEffects;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::setParamsType(std::string paramsType) {
        graph_->passes_[passIndex_].paramsType = std::move(paramsType);
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setParamsData(std::vector<std::byte> paramsData) {
        graph_->passes_[passIndex_].paramsData = std::move(paramsData);
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(RenderGraphPassCallback callback) {
        graph_->passes_[passIndex_].callback = std::move(callback);
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setCommands(RenderGraphCommandList commands) {
        graph_->passes_[passIndex_].commands = std::move(commands).takeCommands();
        return *this;
    }

    RenderGraph::PassBuilder::PassBuilder(RenderGraph& graph, std::size_t passIndex)
        : graph_(&graph), passIndex_(passIndex) {}

    RenderGraphImageHandle RenderGraph::importImage(RenderGraphImageDesc desc) {
        desc.lifetime = RenderGraphImageLifetime::Imported;
        images_.push_back(std::move(desc));
        return RenderGraphImageHandle{
            .index = static_cast<std::uint32_t>(images_.size() - 1),
        };
    }

    RenderGraphImageHandle RenderGraph::createTransientImage(RenderGraphImageDesc desc) {
        desc.lifetime = RenderGraphImageLifetime::Transient;
        desc.initialState = RenderGraphImageState::Undefined;
        desc.initialShaderStage = RenderGraphShaderStage::None;
        desc.finalState = RenderGraphImageState::Undefined;
        desc.finalShaderStage = RenderGraphShaderStage::None;
        images_.push_back(std::move(desc));
        return RenderGraphImageHandle{
            .index = static_cast<std::uint32_t>(images_.size() - 1),
        };
    }

    RenderGraphBufferHandle RenderGraph::importBuffer(RenderGraphBufferDesc desc) {
        desc.lifetime = RenderGraphBufferLifetime::Imported;
        buffers_.push_back(std::move(desc));
        return RenderGraphBufferHandle{
            .index = static_cast<std::uint32_t>(buffers_.size() - 1),
        };
    }

    RenderGraphBufferHandle RenderGraph::createTransientBuffer(RenderGraphBufferDesc desc) {
        desc.lifetime = RenderGraphBufferLifetime::Transient;
        desc.initialState = RenderGraphBufferState::Undefined;
        desc.initialShaderStage = RenderGraphShaderStage::None;
        desc.finalState = RenderGraphBufferState::Undefined;
        desc.finalShaderStage = RenderGraphShaderStage::None;
        buffers_.push_back(std::move(desc));
        return RenderGraphBufferHandle{
            .index = static_cast<std::uint32_t>(buffers_.size() - 1),
        };
    }

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name) {
        Pass pass{
            .name = std::move(name),
            .type = {},
            .paramsType = {},
            .paramsData = {},
            .colorWriteSlots = {},
            .shaderReadSlots = {},
            .depthReadSlots = {},
            .depthWriteSlots = {},
            .depthSampledReadSlots = {},
            .transferReadSlots = {},
            .transferWriteSlots = {},
            .bufferReadSlots = {},
            .bufferTransferReadSlots = {},
            .bufferWriteSlots = {},
            .bufferStorageReadWriteSlots = {},
            .commands = {},
            .allowCulling = {},
            .hasSideEffects = {},
            .callback = {},
        };
        passes_.push_back(std::move(pass));
        return PassBuilder{*this, passes_.size() - 1};
    }

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name, std::string type) {
        Pass pass{
            .name = std::move(name),
            .type = std::move(type),
            .paramsType = {},
            .paramsData = {},
            .colorWriteSlots = {},
            .shaderReadSlots = {},
            .depthReadSlots = {},
            .depthWriteSlots = {},
            .depthSampledReadSlots = {},
            .transferReadSlots = {},
            .transferWriteSlots = {},
            .bufferReadSlots = {},
            .bufferTransferReadSlots = {},
            .bufferWriteSlots = {},
            .bufferStorageReadWriteSlots = {},
            .commands = {},
            .allowCulling = {},
            .hasSideEffects = {},
            .callback = {},
        };
        passes_.push_back(std::move(pass));
        return PassBuilder{*this, passes_.size() - 1};
    }

    Result<RenderGraphCompileResult> RenderGraph::compile() const {
        return compile(nullptr);
    }

    Result<RenderGraphCompileResult>
    RenderGraph::compile(const RenderGraphSchemaRegistry& schemaRegistry) const {
        return compile(&schemaRegistry);
    }

    Result<void> RenderGraph::execute() const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        return execute(*compiled);
    }

    Result<void> RenderGraph::execute(const RenderGraphExecutorRegistry& executorRegistry) const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        return execute(*compiled, executorRegistry);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled) const {
        return execute(compiled, nullptr);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled,
                                      const RenderGraphExecutorRegistry& executorRegistry) const {
        return execute(compiled, &executorRegistry);
    }

} // namespace asharia

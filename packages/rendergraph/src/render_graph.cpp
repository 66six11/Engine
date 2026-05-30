#include "asharia/rendergraph/render_graph.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace asharia {

    namespace {

        std::vector<RenderGraphPassDependency>
        filterActiveDependencies(std::span<const RenderGraphPassDependency> dependencies,
                                 const std::vector<bool>& activePasses) {
            std::vector<RenderGraphPassDependency> activeDependencies;
            activeDependencies.reserve(dependencies.size());
            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex >= activePasses.size() ||
                    dependency.toDeclarationIndex >= activePasses.size()) {
                    continue;
                }
                if (activePasses[dependency.fromDeclarationIndex] &&
                    activePasses[dependency.toDeclarationIndex]) {
                    activeDependencies.push_back(dependency);
                }
            }

            return activeDependencies;
        }

        std::size_t activePassCount(const std::vector<bool>& activePasses) {
            std::size_t count = 0;
            for (const bool active : activePasses) {
                if (active) {
                    ++count;
                }
            }

            return count;
        }

    } // namespace

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

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<RenderGraphCompileResult>
    RenderGraph::compile(const RenderGraphSchemaRegistry* schemaRegistry) const {
        auto imagesValidated = validateImages();
        if (!imagesValidated) {
            return std::unexpected{std::move(imagesValidated.error())};
        }

        auto buffersValidated = validateBuffers();
        if (!buffersValidated) {
            return std::unexpected{std::move(buffersValidated.error())};
        }

        for (const Pass& pass : passes_) {
            auto passValidated = validatePass(pass, schemaRegistry);
            if (!passValidated) {
                return std::unexpected{std::move(passValidated.error())};
            }
        }

        auto dependencies = buildDependencies();
        if (!dependencies) {
            return std::unexpected{std::move(dependencies.error())};
        }

        auto activePasses = findActivePasses(*dependencies, schemaRegistry);
        const std::vector<RenderGraphPassDependency> activeDependencies =
            filterActiveDependencies(*dependencies, activePasses);

        auto passOrder = sortPassesByDependencies(activeDependencies, activePasses);
        if (!passOrder) {
            return std::unexpected{std::move(passOrder.error())};
        }

        std::vector<RenderGraphImageAccess> currentAccesses;
        currentAccesses.reserve(images_.size());
        for (const RenderGraphImageDesc& image : images_) {
            currentAccesses.push_back(RenderGraphImageAccess{
                .state = image.initialState,
                .shaderStage = image.initialShaderStage,
            });
        }

        std::vector<RenderGraphBufferAccess> currentBufferAccesses;
        currentBufferAccesses.reserve(buffers_.size());
        for (const RenderGraphBufferDesc& buffer : buffers_) {
            currentBufferAccesses.push_back(RenderGraphBufferAccess{
                .state = buffer.initialState,
                .shaderStage = buffer.initialShaderStage,
            });
        }

        RenderGraphCompileResult result;
        result.declaredPassCount = passes_.size();
        result.declaredImageCount = images_.size();
        result.declaredBufferCount = buffers_.size();
        result.passes.reserve(activePassCount(activePasses));
        result.dependencies = activeDependencies;
        result.culledPasses = makeCulledPasses(activePasses);

        for (const std::size_t passIndex : *passOrder) {
            const Pass& pass = passes_[passIndex];
            const bool allowPassCulling = passAllowsCulling(pass, schemaRegistry);
            const bool passHasSideEffectsValue = passHasSideEffects(pass, schemaRegistry);

            RenderGraphCompiledPass compiledPass{
                .name = pass.name,
                .type = pass.type,
                .paramsType = pass.paramsType,
                .declarationIndex = passIndex,
                .allowCulling = allowPassCulling,
                .hasSideEffects = passHasSideEffectsValue,
                .paramsData = pass.paramsData,
                .commands = pass.commands,
                .transitionsBefore = {},
                .colorWrites = imageHandles(pass.colorWriteSlots),
                .shaderReads = imageHandles(pass.shaderReadSlots),
                .depthReads = imageHandles(pass.depthReadSlots),
                .depthWrites = imageHandles(pass.depthWriteSlots),
                .depthSampledReads = imageHandles(pass.depthSampledReadSlots),
                .transferReads = imageHandles(pass.transferReadSlots),
                .transferWrites = imageHandles(pass.transferWriteSlots),
                .bufferReads = bufferHandles(pass.bufferReadSlots),
                .bufferTransferReads = bufferHandles(pass.bufferTransferReadSlots),
                .bufferWrites = bufferHandles(pass.bufferWriteSlots),
                .bufferStorageReadWrites = bufferHandles(pass.bufferStorageReadWriteSlots),
                .colorWriteSlots = pass.colorWriteSlots,
                .shaderReadSlots = pass.shaderReadSlots,
                .depthReadSlots = pass.depthReadSlots,
                .depthWriteSlots = pass.depthWriteSlots,
                .depthSampledReadSlots = pass.depthSampledReadSlots,
                .transferReadSlots = pass.transferReadSlots,
                .transferWriteSlots = pass.transferWriteSlots,
                .bufferReadSlots = pass.bufferReadSlots,
                .bufferTransferReadSlots = pass.bufferTransferReadSlots,
                .bufferWriteSlots = pass.bufferWriteSlots,
                .bufferStorageReadWriteSlots = pass.bufferStorageReadWriteSlots,
                .bufferTransitionsBefore = {},
            };

            auto colorTransitions =
                transitionImages(compiledPass.colorWriteSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::ColorAttachment,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!colorTransitions) {
                return std::unexpected{std::move(colorTransitions.error())};
            }

            auto shaderReadTransitions =
                transitionImages(compiledPass.shaderReadSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::ShaderRead,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!shaderReadTransitions) {
                return std::unexpected{std::move(shaderReadTransitions.error())};
            }

            auto depthReadTransitions =
                transitionImages(compiledPass.depthReadSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::DepthAttachmentRead,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!depthReadTransitions) {
                return std::unexpected{std::move(depthReadTransitions.error())};
            }

            auto depthWriteTransitions =
                transitionImages(compiledPass.depthWriteSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::DepthAttachmentWrite,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!depthWriteTransitions) {
                return std::unexpected{std::move(depthWriteTransitions.error())};
            }

            auto depthSampledReadTransitions =
                transitionImages(compiledPass.depthSampledReadSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::DepthSampledRead,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!depthSampledReadTransitions) {
                return std::unexpected{std::move(depthSampledReadTransitions.error())};
            }

            auto transferReadTransitions =
                transitionImages(compiledPass.transferReadSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::TransferSrc,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!transferReadTransitions) {
                return std::unexpected{std::move(transferReadTransitions.error())};
            }

            auto transferTransitions =
                transitionImages(compiledPass.transferWriteSlots,
                                 RenderGraphImageAccess{
                                     .state = RenderGraphImageState::TransferDst,
                                     .shaderStage = RenderGraphShaderStage::None,
                                 },
                                 currentAccesses, compiledPass);
            if (!transferTransitions) {
                return std::unexpected{std::move(transferTransitions.error())};
            }

            auto bufferWriteTransitions =
                transitionBuffers(compiledPass.bufferWriteSlots,
                                  RenderGraphBufferAccess{
                                      .state = RenderGraphBufferState::TransferWrite,
                                      .shaderStage = RenderGraphShaderStage::None,
                                  },
                                  currentBufferAccesses, compiledPass);
            if (!bufferWriteTransitions) {
                return std::unexpected{std::move(bufferWriteTransitions.error())};
            }

            auto bufferTransferReadTransitions =
                transitionBuffers(compiledPass.bufferTransferReadSlots,
                                  RenderGraphBufferAccess{
                                      .state = RenderGraphBufferState::TransferRead,
                                      .shaderStage = RenderGraphShaderStage::None,
                                  },
                                  currentBufferAccesses, compiledPass);
            if (!bufferTransferReadTransitions) {
                return std::unexpected{std::move(bufferTransferReadTransitions.error())};
            }

            auto bufferReadTransitions =
                transitionBuffers(compiledPass.bufferReadSlots,
                                  RenderGraphBufferAccess{
                                      .state = RenderGraphBufferState::ShaderRead,
                                      .shaderStage = RenderGraphShaderStage::None,
                                  },
                                  currentBufferAccesses, compiledPass);
            if (!bufferReadTransitions) {
                return std::unexpected{std::move(bufferReadTransitions.error())};
            }

            auto bufferStorageReadWriteTransitions =
                transitionBuffers(compiledPass.bufferStorageReadWriteSlots,
                                  RenderGraphBufferAccess{
                                      .state = RenderGraphBufferState::StorageReadWrite,
                                      .shaderStage = RenderGraphShaderStage::None,
                                  },
                                  currentBufferAccesses, compiledPass);
            if (!bufferStorageReadWriteTransitions) {
                return std::unexpected{std::move(bufferStorageReadWriteTransitions.error())};
            }

            result.passes.push_back(std::move(compiledPass));
        }

        for (std::size_t index = 0; index < images_.size(); ++index) {
            const RenderGraphImageDesc& image = images_[index];
            if (image.lifetime == RenderGraphImageLifetime::Transient) {
                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                if (!imageUsedByCompiledPasses(result.passes, imageHandle)) {
                    if (imageUsedByDeclaredPasses(imageHandle)) {
                        continue;
                    }
                }

                auto allocation =
                    makeTransientAllocation(index, result.passes, currentAccesses[index]);
                if (!allocation) {
                    return std::unexpected{std::move(allocation.error())};
                }

                result.transientImages.push_back(std::move(*allocation));
                continue;
            }

            const RenderGraphImageAccess finalAccess{
                .state = image.finalState,
                .shaderStage = image.finalShaderStage,
            };
            if (currentAccesses[index] == finalAccess) {
                continue;
            }

            const RenderGraphImageHandle imageHandle{
                .index = static_cast<std::uint32_t>(index),
            };
            result.finalTransitions.push_back(
                makeTransition(imageHandle, image, currentAccesses[index], finalAccess));
        }

        for (std::size_t index = 0; index < buffers_.size(); ++index) {
            const RenderGraphBufferDesc& buffer = buffers_[index];
            if (buffer.lifetime == RenderGraphBufferLifetime::Transient) {
                const RenderGraphBufferHandle bufferHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                if (!bufferUsedByCompiledPasses(result.passes, bufferHandle)) {
                    if (bufferUsedByDeclaredPasses(bufferHandle)) {
                        continue;
                    }
                }

                auto allocation = makeTransientBufferAllocation(index, result.passes,
                                                                currentBufferAccesses[index]);
                if (!allocation) {
                    return std::unexpected{std::move(allocation.error())};
                }

                result.transientBuffers.push_back(std::move(*allocation));
                continue;
            }

            const RenderGraphBufferAccess finalAccess{
                .state = buffer.finalState,
                .shaderStage = buffer.finalShaderStage,
            };
            if (currentBufferAccesses[index] == finalAccess) {
                continue;
            }

            const RenderGraphBufferHandle bufferHandle{
                .index = static_cast<std::uint32_t>(index),
            };
            result.finalBufferTransitions.push_back(
                makeTransition(bufferHandle, buffer, currentBufferAccesses[index], finalAccess));
        }

        return result;
    }
    // NOLINTEND(readability-function-cognitive-complexity)

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

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled,
                                      const RenderGraphExecutorRegistry* executorRegistry) const {
        if (compiled.declaredPassCount != passes_.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Compiled render graph declaration count does not match the graph.",
            }};
        }

        std::vector<bool> executedDeclarations(passes_.size());
        for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
            const RenderGraphCompiledPass& pass = compiled.passes[index];
            if (pass.declarationIndex >= passes_.size() ||
                passes_[pass.declarationIndex].name != pass.name) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Compiled render graph pass '" + pass.name +
                        "' does not match the graph declaration.",
                }};
            }
            if (executedDeclarations[pass.declarationIndex]) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Compiled render graph pass '" + pass.name + "' appears more than once.",
                }};
            }
            executedDeclarations[pass.declarationIndex] = true;

            const RenderGraphPassCallback* callback = &passes_[pass.declarationIndex].callback;
            if (!*callback && executorRegistry != nullptr) {
                callback = executorRegistry->find(pass.type);
            }
            if (callback == nullptr || !*callback) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    missingCallbackMessage(pass),
                }};
            }

            auto executed = (*callback)(RenderGraphPassContext{
                .passIndex = index,
                .declarationIndex = pass.declarationIndex,
                .name = pass.name,
                .type = pass.type,
                .paramsType = pass.paramsType,
                .allowCulling = pass.allowCulling,
                .hasSideEffects = pass.hasSideEffects,
                .paramsData = pass.paramsData,
                .commands = pass.commands,
                .transitionsBefore = pass.transitionsBefore,
                .colorWrites = pass.colorWrites,
                .shaderReads = pass.shaderReads,
                .depthReads = pass.depthReads,
                .depthWrites = pass.depthWrites,
                .depthSampledReads = pass.depthSampledReads,
                .transferReads = pass.transferReads,
                .transferWrites = pass.transferWrites,
                .bufferReads = pass.bufferReads,
                .bufferTransferReads = pass.bufferTransferReads,
                .bufferWrites = pass.bufferWrites,
                .bufferStorageReadWrites = pass.bufferStorageReadWrites,
                .colorWriteSlots = pass.colorWriteSlots,
                .shaderReadSlots = pass.shaderReadSlots,
                .depthReadSlots = pass.depthReadSlots,
                .depthWriteSlots = pass.depthWriteSlots,
                .depthSampledReadSlots = pass.depthSampledReadSlots,
                .transferReadSlots = pass.transferReadSlots,
                .transferWriteSlots = pass.transferWriteSlots,
                .bufferReadSlots = pass.bufferReadSlots,
                .bufferTransferReadSlots = pass.bufferTransferReadSlots,
                .bufferWriteSlots = pass.bufferWriteSlots,
                .bufferStorageReadWriteSlots = pass.bufferStorageReadWriteSlots,
                .bufferTransitionsBefore = pass.bufferTransitionsBefore,
            });
            if (!executed) {
                return std::unexpected{std::move(executed.error())};
            }
        }

        return {};
    }

    Result<void> RenderGraph::validateImages() const {
        for (const RenderGraphImageDesc& image : images_) {
            if (image.lifetime == RenderGraphImageLifetime::Imported &&
                image.finalState == RenderGraphImageState::Undefined) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Imported render graph image '" + image.name +
                        "' must declare an explicit final state.",
                }};
            }
        }

        return {};
    }

    Result<void> RenderGraph::validateBuffers() const {
        for (const RenderGraphBufferDesc& buffer : buffers_) {
            if (buffer.byteSize == 0) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph buffer '" + buffer.name + "' must declare a non-zero byte size.",
                }};
            }
            if (buffer.lifetime == RenderGraphBufferLifetime::Imported &&
                buffer.finalState == RenderGraphBufferState::Undefined) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Imported render graph buffer '" + buffer.name +
                        "' must declare an explicit final state.",
                }};
            }
        }

        return {};
    }

    Result<void> RenderGraph::validatePass(const Pass& pass,
                                           const RenderGraphSchemaRegistry* schemaRegistry) const {
        auto slotsValidated = validateWriteSlots(pass);
        if (!slotsValidated) {
            return std::unexpected{std::move(slotsValidated.error())};
        }

        if (schemaRegistry != nullptr) {
            auto schemaValidated = validateSchema(pass, *schemaRegistry);
            if (!schemaValidated) {
                return std::unexpected{std::move(schemaValidated.error())};
            }
        }

        return {};
    }

    Result<std::vector<RenderGraphPassDependency>> RenderGraph::buildDependencies() const {
        std::vector<RenderGraphPassDependency> dependencies;

        auto imageDependencies = buildImageDependencies(dependencies);
        if (!imageDependencies) {
            return std::unexpected{std::move(imageDependencies.error())};
        }

        auto bufferDependencies = buildBufferDependencies(dependencies);
        if (!bufferDependencies) {
            return std::unexpected{std::move(bufferDependencies.error())};
        }

        return dependencies;
    }

    Result<void> RenderGraph::buildImageDependencies(
        std::vector<RenderGraphPassDependency>& dependencies) const {
        for (std::size_t imageIndex = 0; imageIndex < images_.size(); ++imageIndex) {
            const RenderGraphImageHandle imageHandle{
                .index = static_cast<std::uint32_t>(imageIndex),
            };
            std::vector<std::size_t> writers;
            std::vector<std::size_t> readers;

            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                const Pass& pass = passes_[passIndex];
                if (passWritesImage(pass, imageHandle)) {
                    writers.push_back(passIndex);
                }
                if (passReadsImage(pass, imageHandle)) {
                    readers.push_back(passIndex);
                }
            }

            for (std::size_t writerIndex = 1; writerIndex < writers.size(); ++writerIndex) {
                addDependency(dependencies, writers[writerIndex - 1], writers[writerIndex],
                              imageHandle, "write order");
            }

            auto readDependencies =
                addReadDependencies(dependencies, imageHandle, writers, readers);
            if (!readDependencies) {
                return std::unexpected{std::move(readDependencies.error())};
            }
        }

        return {};
    }

    Result<void> RenderGraph::buildBufferDependencies(
        std::vector<RenderGraphPassDependency>& dependencies) const {
        for (std::size_t bufferIndex = 0; bufferIndex < buffers_.size(); ++bufferIndex) {
            const RenderGraphBufferHandle bufferHandle{
                .index = static_cast<std::uint32_t>(bufferIndex),
            };
            std::vector<std::size_t> writers;
            std::vector<std::size_t> readers;

            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                const Pass& pass = passes_[passIndex];
                if (passWritesBuffer(pass, bufferHandle)) {
                    writers.push_back(passIndex);
                }
                if (passReadsBuffer(pass, bufferHandle)) {
                    readers.push_back(passIndex);
                }
            }

            for (std::size_t writerIndex = 1; writerIndex < writers.size(); ++writerIndex) {
                addBufferDependency(dependencies, writers[writerIndex - 1], writers[writerIndex],
                                    bufferHandle, "write order");
            }

            auto readDependencies =
                addBufferReadDependencies(dependencies, bufferHandle, writers, readers);
            if (!readDependencies) {
                return std::unexpected{std::move(readDependencies.error())};
            }
        }

        return {};
    }

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    RenderGraphDiagnosticsSnapshot
    RenderGraph::diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const {
        RenderGraphDiagnosticsSnapshot snapshot;
        snapshot.declaredPassCount = compiled.declaredPassCount;
        snapshot.declaredImageCount = compiled.declaredImageCount;
        snapshot.declaredBufferCount = compiled.declaredBufferCount;
        snapshot.passes.reserve(compiled.passes.size());
        snapshot.resources.reserve(images_.size() + buffers_.size());
        snapshot.dependencyEdges.reserve(compiled.dependencies.size());
        snapshot.transitions.reserve(compiled.finalTransitions.size() +
                                     compiled.finalBufferTransitions.size());
        snapshot.culledPasses = compiled.culledPasses;
        snapshot.transientImages = compiled.transientImages;
        snapshot.transientBuffers = compiled.transientBuffers;

        for (std::size_t imageIndex = 0; imageIndex < images_.size(); ++imageIndex) {
            const RenderGraphImageDesc& image = images_[imageIndex];
            snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                .kind = RenderGraphResourceKind::Image,
                .resourceIndex = static_cast<std::uint32_t>(imageIndex),
                .name = image.name,
                .imageLifetime = image.lifetime,
                .imageFormat = image.format,
                .imageExtent = image.extent,
                .imageInitialAccess =
                    RenderGraphImageAccess{
                        .state = image.initialState,
                        .shaderStage = image.initialShaderStage,
                    },
                .imageFinalAccess =
                    RenderGraphImageAccess{
                        .state = image.finalState,
                        .shaderStage = image.finalShaderStage,
                    },
            });
        }

        for (std::size_t bufferIndex = 0; bufferIndex < buffers_.size(); ++bufferIndex) {
            const RenderGraphBufferDesc& buffer = buffers_[bufferIndex];
            snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                .kind = RenderGraphResourceKind::Buffer,
                .resourceIndex = static_cast<std::uint32_t>(bufferIndex),
                .name = buffer.name,
                .bufferLifetime = buffer.lifetime,
                .bufferByteSize = buffer.byteSize,
                .bufferInitialAccess =
                    RenderGraphBufferAccess{
                        .state = buffer.initialState,
                        .shaderStage = buffer.initialShaderStage,
                    },
                .bufferFinalAccess =
                    RenderGraphBufferAccess{
                        .state = buffer.finalState,
                        .shaderStage = buffer.finalShaderStage,
                    },
            });
        }

        const auto imageName = [this](RenderGraphImageHandle image) -> std::string {
            if (image.index < images_.size()) {
                return images_[image.index].name;
            }
            return {};
        };
        const auto bufferName = [this](RenderGraphBufferHandle buffer) -> std::string {
            if (buffer.index < buffers_.size()) {
                return buffers_[buffer.index].name;
            }
            return {};
        };
        const auto compiledPassIndex = [&compiled](std::size_t declarationIndex) -> std::size_t {
            for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
                if (compiled.passes[passIndex].declarationIndex == declarationIndex) {
                    return passIndex;
                }
            }
            return compiled.passes.size();
        };
        const auto appendImageEdges =
            [&imageName, &snapshot](std::size_t passIndex, const RenderGraphCompiledPass& pass,
                                    RenderGraphSlotAccess access,
                                    std::span<const RenderGraphImageSlot> slots) {
                for (const RenderGraphImageSlot& slot : slots) {
                    snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                        .passIndex = passIndex,
                        .declarationIndex = pass.declarationIndex,
                        .passName = pass.name,
                        .resourceKind = RenderGraphResourceKind::Image,
                        .resourceIndex = slot.image.index,
                        .resourceName = imageName(slot.image),
                        .slotName = slot.name,
                        .access = access,
                        .shaderStage = slot.shaderStage,
                    });
                }
            };
        const auto appendBufferEdges =
            [&bufferName, &snapshot](std::size_t passIndex, const RenderGraphCompiledPass& pass,
                                     RenderGraphSlotAccess access,
                                     std::span<const RenderGraphBufferSlot> slots) {
                for (const RenderGraphBufferSlot& slot : slots) {
                    snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                        .passIndex = passIndex,
                        .declarationIndex = pass.declarationIndex,
                        .passName = pass.name,
                        .resourceKind = RenderGraphResourceKind::Buffer,
                        .resourceIndex = slot.buffer.index,
                        .resourceName = bufferName(slot.buffer),
                        .slotName = slot.name,
                        .access = access,
                        .shaderStage = slot.shaderStage,
                    });
                }
            };

        for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
            const RenderGraphCompiledPass& pass = compiled.passes[passIndex];
            snapshot.passes.push_back(RenderGraphDiagnosticsPassNode{
                .passIndex = passIndex,
                .declarationIndex = pass.declarationIndex,
                .name = pass.name,
                .type = pass.type,
                .paramsType = pass.paramsType,
                .allowCulling = pass.allowCulling,
                .hasSideEffects = pass.hasSideEffects,
                .commandCount = pass.commands.size(),
                .imageTransitionCount = pass.transitionsBefore.size(),
                .bufferTransitionCount = pass.bufferTransitionsBefore.size(),
            });

            for (std::size_t commandIndex = 0; commandIndex < pass.commands.size();
                 ++commandIndex) {
                const RenderGraphCommand& command = pass.commands[commandIndex];
                snapshot.commands.push_back(RenderGraphDiagnosticsCommandNode{
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .commandIndex = commandIndex,
                    .passName = pass.name,
                    .kind = command.kind,
                    .detail = commandDetail(command),
                });
            }

            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::ColorWrite,
                             pass.colorWriteSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::ShaderRead,
                             pass.shaderReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthAttachmentRead,
                             pass.depthReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthAttachmentWrite,
                             pass.depthWriteSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthSampledRead,
                             pass.depthSampledReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::TransferRead,
                             pass.transferReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::TransferWrite,
                             pass.transferWriteSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferShaderRead,
                              pass.bufferReadSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferTransferRead,
                              pass.bufferTransferReadSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferTransferWrite,
                              pass.bufferWriteSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferStorageReadWrite,
                              pass.bufferStorageReadWriteSlots);

            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                    .phase = RenderGraphDiagnosticsTransitionPhase::BeforePass,
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Image,
                    .resourceIndex = transition.image.index,
                    .resourceName = transition.imageName,
                    .oldImageAccess =
                        RenderGraphImageAccess{
                            .state = transition.oldState,
                            .shaderStage = transition.oldShaderStage,
                        },
                    .newImageAccess =
                        RenderGraphImageAccess{
                            .state = transition.newState,
                            .shaderStage = transition.newShaderStage,
                        },
                });
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                    .phase = RenderGraphDiagnosticsTransitionPhase::BeforePass,
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Buffer,
                    .resourceIndex = transition.buffer.index,
                    .resourceName = transition.bufferName,
                    .oldBufferAccess =
                        RenderGraphBufferAccess{
                            .state = transition.oldState,
                            .shaderStage = transition.oldShaderStage,
                        },
                    .newBufferAccess =
                        RenderGraphBufferAccess{
                            .state = transition.newState,
                            .shaderStage = transition.newShaderStage,
                        },
                });
            }
        }

        for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
            snapshot.dependencyEdges.push_back(RenderGraphDiagnosticsDependencyEdge{
                .fromPassIndex = compiledPassIndex(dependency.fromDeclarationIndex),
                .toPassIndex = compiledPassIndex(dependency.toDeclarationIndex),
                .fromDeclarationIndex = dependency.fromDeclarationIndex,
                .toDeclarationIndex = dependency.toDeclarationIndex,
                .resourceKind = dependency.resourceKind,
                .resourceIndex = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                     ? dependency.buffer.index
                                     : dependency.image.index,
                .resourceName = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                    ? dependency.bufferName
                                    : dependency.imageName,
                .reason = dependency.reason,
            });
        }

        for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = RenderGraphDiagnosticsTransitionPhase::Final,
                .passIndex = compiled.passes.size(),
                .declarationIndex = compiled.declaredPassCount,
                .passName = {},
                .resourceKind = RenderGraphResourceKind::Image,
                .resourceIndex = transition.image.index,
                .resourceName = transition.imageName,
                .oldImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }
        for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = RenderGraphDiagnosticsTransitionPhase::Final,
                .passIndex = compiled.passes.size(),
                .declarationIndex = compiled.declaredPassCount,
                .passName = {},
                .resourceKind = RenderGraphResourceKind::Buffer,
                .resourceIndex = transition.buffer.index,
                .resourceName = transition.bufferName,
                .oldBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }

        return snapshot;
    }
    // NOLINTEND(readability-function-cognitive-complexity)

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    std::string RenderGraph::formatDebugTables(const RenderGraphCompileResult& compiled) const {
        std::string output;
        output += "### RenderGraph Resources\n\n";
        output += "| # | Name | Lifetime | Format | Extent | Initial | Final |\n";
        output += "|---:|---|---|---|---:|---|---|\n";
        for (std::size_t index = 0; index < images_.size(); ++index) {
            const RenderGraphImageDesc& image = images_[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += image.name;
            output += " | ";
            output += imageLifetimeName(image.lifetime);
            output += " | ";
            output += imageFormatName(image.format);
            output += " | ";
            output += std::to_string(image.extent.width);
            output += "x";
            output += std::to_string(image.extent.height);
            output += " | ";
            output += imageAccessName(image.initialState, image.initialShaderStage);
            output += " | ";
            output += imageAccessName(image.finalState, image.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Buffers\n\n";
        output += "| # | Name | Lifetime | Bytes | Initial | Final |\n";
        output += "|---:|---|---|---:|---|---|\n";
        for (std::size_t index = 0; index < buffers_.size(); ++index) {
            const RenderGraphBufferDesc& buffer = buffers_[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += buffer.name;
            output += " | ";
            output += bufferLifetimeName(buffer.lifetime);
            output += " | ";
            output += std::to_string(buffer.byteSize);
            output += " | ";
            output += bufferAccessName(buffer.initialState, buffer.initialShaderStage);
            output += " | ";
            output += bufferAccessName(buffer.finalState, buffer.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Passes\n\n";
        output += "| # | Decl # | Name | Type | Params | Cullable | Side Effects | "
                  "Before Transitions | Buffer Transitions | Color Writes | Shader Reads | "
                  "Depth Reads | Depth Writes | Depth Sampled Reads | Transfer Reads | "
                  "Transfer Writes | Buffer Reads | Buffer Transfer Reads | Buffer Writes | "
                  "Buffer Storage Read/Writes |\n";
        output += "|---:|---:|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|---|---"
                  "|---|\n";
        for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
            const RenderGraphCompiledPass& pass = compiled.passes[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += std::to_string(pass.declarationIndex);
            output += " | ";
            output += pass.name;
            output += " | ";
            output += pass.type.empty() ? "-" : pass.type;
            output += " | ";
            output += pass.paramsType.empty() ? "-" : pass.paramsType;
            output += " | ";
            output += pass.allowCulling ? "yes" : "no";
            output += " | ";
            output += pass.hasSideEffects ? "yes" : "no";
            output += " | ";
            output += std::to_string(pass.transitionsBefore.size());
            output += " | ";
            output += std::to_string(pass.bufferTransitionsBefore.size());
            output += " | ";
            output += imageSlotList(pass.colorWriteSlots);
            output += " | ";
            output += imageSlotList(pass.shaderReadSlots);
            output += " | ";
            output += imageSlotList(pass.depthReadSlots);
            output += " | ";
            output += imageSlotList(pass.depthWriteSlots);
            output += " | ";
            output += imageSlotList(pass.depthSampledReadSlots);
            output += " | ";
            output += imageSlotList(pass.transferReadSlots);
            output += " | ";
            output += imageSlotList(pass.transferWriteSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferReadSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferTransferReadSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferWriteSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferStorageReadWriteSlots);
            output += " |\n";
        }

        output += "\n### RenderGraph Dependencies\n\n";
        output += "| From | To | Resource | Reason |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
            output += "| ";
            output += passDeclarationLabel(dependency.fromDeclarationIndex);
            output += " | ";
            output += passDeclarationLabel(dependency.toDeclarationIndex);
            output += " | ";
            output += dependencyResourceLabel(dependency);
            output += " | ";
            output += dependency.reason;
            output += " |\n";
        }

        output += "\n### RenderGraph Culled Passes\n\n";
        output += "| Decl # | Name | Type | Reason |\n";
        output += "|---:|---|---|---|\n";
        for (const RenderGraphCulledPass& pass : compiled.culledPasses) {
            output += "| ";
            output += std::to_string(pass.declarationIndex);
            output += " | ";
            output += pass.name;
            output += " | ";
            output += pass.type.empty() ? "-" : pass.type;
            output += " | ";
            output += pass.reason;
            output += " |\n";
        }

        output += "\n### RenderGraph Slots\n\n";
        output += "| Pass | Access | Slot | Resource |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendSlotRows(output, pass.name, "ColorWrite", pass.colorWriteSlots);
            appendSlotRows(output, pass.name, "ShaderRead", pass.shaderReadSlots);
            appendSlotRows(output, pass.name, "DepthAttachmentRead", pass.depthReadSlots);
            appendSlotRows(output, pass.name, "DepthAttachmentWrite", pass.depthWriteSlots);
            appendSlotRows(output, pass.name, "DepthSampledRead", pass.depthSampledReadSlots);
            appendSlotRows(output, pass.name, "TransferRead", pass.transferReadSlots);
            appendSlotRows(output, pass.name, "TransferWrite", pass.transferWriteSlots);
            appendBufferSlotRows(output, pass.name, "BufferShaderRead", pass.bufferReadSlots);
            appendBufferSlotRows(output, pass.name, "BufferTransferRead",
                                 pass.bufferTransferReadSlots);
            appendBufferSlotRows(output, pass.name, "BufferTransferWrite", pass.bufferWriteSlots);
            appendBufferSlotRows(output, pass.name, "BufferStorageReadWrite",
                                 pass.bufferStorageReadWriteSlots);
        }

        output += "\n### RenderGraph Commands\n\n";
        output += "| Pass | # | Command | Detail |\n";
        output += "|---|---:|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendCommandRows(output, pass);
        }

        output += "\n### RenderGraph Transitions\n\n";
        output += "| Phase | Pass | Resource | Old State | New State |\n";
        output += "|---|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                appendTransitionRow(output, "Before", pass.name, transition);
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                appendTransitionRow(output, "Before", pass.name, transition);
            }
        }
        for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
            appendTransitionRow(output, "Final", "-", transition);
        }
        for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
            appendTransitionRow(output, "Final", "-", transition);
        }

        output += "\n### RenderGraph Transients\n\n";
        output += "| Image | Format | Extent | First Pass | Last Pass | Final Access |\n";
        output += "|---|---|---:|---:|---:|---|\n";
        for (const RenderGraphTransientImageAllocation& transient : compiled.transientImages) {
            output += "| ";
            output += imageHandleLabel(transient.image);
            output += " | ";
            output += imageFormatName(transient.format);
            output += " | ";
            output += std::to_string(transient.extent.width);
            output += "x";
            output += std::to_string(transient.extent.height);
            output += " | ";
            output += std::to_string(transient.firstPassIndex);
            output += " | ";
            output += std::to_string(transient.lastPassIndex);
            output += " | ";
            output += imageAccessName(transient.finalState, transient.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Transient Buffers\n\n";
        output += "| Buffer | Bytes | First Pass | Last Pass | Final Access |\n";
        output += "|---|---:|---:|---:|---|\n";
        for (const RenderGraphTransientBufferAllocation& transient : compiled.transientBuffers) {
            output += "| ";
            output += bufferHandleLabel(transient.buffer);
            output += " | ";
            output += std::to_string(transient.byteSize);
            output += " | ";
            output += std::to_string(transient.firstPassIndex);
            output += " | ";
            output += std::to_string(transient.lastPassIndex);
            output += " | ";
            output += bufferAccessName(transient.finalState, transient.finalShaderStage);
            output += " |\n";
        }

        return output;
    }
    // NOLINTEND(readability-function-cognitive-complexity)

} // namespace asharia

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

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

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<RenderGraphCompileResult>
    RenderGraph::Impl::compile(const RenderGraphSchemaRegistry* schemaRegistry) const {
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
} // namespace asharia

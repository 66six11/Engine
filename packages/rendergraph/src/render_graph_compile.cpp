#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "render_graph_compiled_pass.hpp"
#include "render_graph_dependency_builder.hpp"
#include "render_graph_dependency_culling.hpp"
#include "render_graph_dependency_sort.hpp"
#include "render_graph_internal.hpp"
#include "render_graph_lifetime.hpp"
#include "render_graph_operations.hpp"
#include "render_graph_pass_queries.hpp"
#include "render_graph_validation.hpp"

namespace asharia {

    Result<RenderGraphCompileResult> RenderGraph::compile() const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_});
        return rendergraph_internal::compileRenderGraph(declarations, nullptr);
    }

    Result<RenderGraphCompileResult>
    RenderGraph::compile(const RenderGraphSchemaRegistry& schemaRegistry) const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_});
        return rendergraph_internal::compileRenderGraph(declarations, &schemaRegistry);
    }

} // namespace asharia

namespace asharia::rendergraph_internal {

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

        std::size_t compiledPassCount(const std::vector<bool>& activePasses) {
            std::size_t count = 0;
            for (const bool active : activePasses) {
                if (active) {
                    ++count;
                }
            }

            return count;
        }

        [[nodiscard]] std::vector<RenderGraphCulledPass>
        makeCulledPasses(std::span<const Pass> passes, const std::vector<bool>& activePasses) {
            std::vector<RenderGraphCulledPass> culledPasses;
            for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
                if (passIndex < activePasses.size() && activePasses[passIndex]) {
                    continue;
                }

                const Pass& pass = passes[passIndex];
                culledPasses.push_back(RenderGraphCulledPass{
                    .declarationIndex = passIndex,
                    .name = pass.name,
                    .type = pass.type,
                    .reason = "cullable pass has no active consumers or side effects",
                });
            }

            return culledPasses;
        }

    } // namespace

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<RenderGraphCompileResult>
    compileRenderGraph(RenderGraphDeclarationView declarations,
                       const RenderGraphSchemaRegistry* schemaRegistry) {
        auto imagesValidated = validateImages(declarations.images);
        if (!imagesValidated) {
            return std::unexpected{std::move(imagesValidated.error())};
        }

        auto buffersValidated = validateBuffers(declarations.buffers);
        if (!buffersValidated) {
            return std::unexpected{std::move(buffersValidated.error())};
        }

        for (const Pass& pass : declarations.passes) {
            auto passValidated =
                validatePass(declarations.images, declarations.buffers, pass, schemaRegistry);
            if (!passValidated) {
                return std::unexpected{std::move(passValidated.error())};
            }
        }

        auto dependencies = buildDependencies(DependencyBuildInputs{
            .images = declarations.images,
            .buffers = declarations.buffers,
            .passes = declarations.passes,
        });
        if (!dependencies) {
            return std::unexpected{std::move(dependencies.error())};
        }

        auto activePasses = findActivePasses(declarations.passes, declarations.images,
                                             declarations.buffers, *dependencies, schemaRegistry);
        const std::vector<RenderGraphPassDependency> activeDependencies =
            filterActiveDependencies(*dependencies, activePasses);

        auto passOrder =
            sortPassesByDependencies(declarations.passes, activeDependencies, activePasses);
        if (!passOrder) {
            return std::unexpected{std::move(passOrder.error())};
        }

        std::vector<RenderGraphImageAccess> currentAccesses;
        currentAccesses.reserve(declarations.images.size());
        for (const RenderGraphImageDesc& image : declarations.images) {
            currentAccesses.push_back(RenderGraphImageAccess{
                .state = image.initialState,
                .shaderStage = image.initialShaderStage,
            });
        }

        std::vector<RenderGraphBufferAccess> currentBufferAccesses;
        currentBufferAccesses.reserve(declarations.buffers.size());
        for (const RenderGraphBufferDesc& buffer : declarations.buffers) {
            currentBufferAccesses.push_back(RenderGraphBufferAccess{
                .state = buffer.initialState,
                .shaderStage = buffer.initialShaderStage,
            });
        }

        RenderGraphCompileResult result;
        result.declaredPassCount = declarations.passes.size();
        result.declaredImageCount = declarations.images.size();
        result.declaredBufferCount = declarations.buffers.size();
        result.passes.reserve(compiledPassCount(activePasses));
        result.dependencies = activeDependencies;
        result.culledPasses = makeCulledPasses(declarations.passes, activePasses);

        for (const std::size_t passIndex : *passOrder) {
            const Pass& pass = declarations.passes[passIndex];
            RenderGraphCompiledPass compiledPass =
                makeCompiledPass(pass, passIndex, schemaRegistry);

            auto transitions =
                appendCompiledPassTransitions(declarations.images, declarations.buffers,
                                              currentAccesses, currentBufferAccesses, compiledPass);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            result.passes.push_back(std::move(compiledPass));
        }

        for (std::size_t index = 0; index < declarations.images.size(); ++index) {
            const RenderGraphImageDesc& image = declarations.images[index];
            if (image.lifetime == RenderGraphImageLifetime::Transient) {
                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                if (!imageUsedByCompiledPasses(result.passes, imageHandle)) {
                    if (imageUsedByDeclaredPasses(declarations.passes, imageHandle)) {
                        continue;
                    }
                }

                auto allocation = makeTransientAllocation(declarations.images, index, result.passes,
                                                          currentAccesses[index]);
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

        for (std::size_t index = 0; index < declarations.buffers.size(); ++index) {
            const RenderGraphBufferDesc& buffer = declarations.buffers[index];
            if (buffer.lifetime == RenderGraphBufferLifetime::Transient) {
                const RenderGraphBufferHandle bufferHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                if (!bufferUsedByCompiledPasses(result.passes, bufferHandle)) {
                    if (bufferUsedByDeclaredPasses(declarations.passes, bufferHandle)) {
                        continue;
                    }
                }

                auto allocation = makeTransientBufferAllocation(
                    declarations.buffers, index, result.passes, currentBufferAccesses[index]);
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
} // namespace asharia::rendergraph_internal

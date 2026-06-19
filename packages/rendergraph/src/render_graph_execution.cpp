#include "asharia/rendergraph/render_graph_execution.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"
#include "render_graph_operations.hpp"

namespace asharia {

    Result<void> RenderGraph::execute() const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_},
                impl_->mutationGeneration_);
        return rendergraph_internal::executeRenderGraph(declarations, *compiled, nullptr);
    }

    Result<void> RenderGraph::execute(const RenderGraphExecutorRegistry& executorRegistry) const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_},
                impl_->mutationGeneration_);
        return rendergraph_internal::executeRenderGraph(declarations, *compiled, &executorRegistry);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled) const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_},
                impl_->mutationGeneration_);
        return rendergraph_internal::executeRenderGraph(declarations, compiled, nullptr);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled,
                                      const RenderGraphExecutorRegistry& executorRegistry) const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_},
                impl_->mutationGeneration_);
        return rendergraph_internal::executeRenderGraph(declarations, compiled, &executorRegistry);
    }

} // namespace asharia

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] std::string missingCallbackMessage(const RenderGraphCompiledPass& pass) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "'";
            if (!pass.type.empty()) {
                message += " of type '";
                message += pass.type;
                message += "'";
            }
            message += " is missing an execute callback.";
            return message;
        }

    } // namespace

    Result<void> executeRenderGraph(RenderGraphDeclarationView declarations,
                                    const RenderGraphCompileResult& compiled,
                                    const RenderGraphExecutorRegistry* executorRegistry) {
        if (compiled.declarationGeneration != declarations.mutationGeneration ||
            compiled.declaredPassCount != declarations.passes.size() ||
            compiled.declaredImageCount != declarations.images.size() ||
            compiled.declaredBufferCount != declarations.buffers.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Compiled render graph result no longer matches the graph; the graph changed "
                "since compile.",
            }};
        }

        std::vector<bool> executedDeclarations(declarations.passes.size());
        for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
            const RenderGraphCompiledPass& pass = compiled.passes[index];
            if (pass.declarationIndex >= declarations.passes.size() ||
                declarations.passes[pass.declarationIndex].name != pass.name) {
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

            const RenderGraphPassCallback* callback =
                &declarations.passes[pass.declarationIndex].callback;
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
} // namespace asharia::rendergraph_internal

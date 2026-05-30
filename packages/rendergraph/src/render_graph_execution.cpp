#include <cstddef>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

namespace asharia {

    Result<void>
    RenderGraph::Impl::execute(const RenderGraphCompileResult& compiled,
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
} // namespace asharia

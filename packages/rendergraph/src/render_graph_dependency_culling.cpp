#include "render_graph_dependency_culling.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "render_graph_pass_queries.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] bool
        passWritesImportedResource(std::span<const RenderGraphImageDesc> images,
                                   std::span<const RenderGraphBufferDesc> buffers,
                                   const Pass& pass) {
            const std::array<std::span<const RenderGraphImageSlot>, 3> writeSlotGroups{
                pass.colorWriteSlots,
                pass.depthWriteSlots,
                pass.transferWriteSlots,
            };
            for (std::span<const RenderGraphImageSlot> slots : writeSlotGroups) {
                for (const RenderGraphImageSlot& slot : slots) {
                    if (slot.image.index < images.size() &&
                        images[slot.image.index].lifetime == RenderGraphImageLifetime::Imported) {
                        return true;
                    }
                }
            }

            const std::array<std::span<const RenderGraphBufferSlot>, 2> bufferWriteSlotGroups{
                pass.bufferWriteSlots,
                pass.bufferStorageReadWriteSlots,
            };
            for (std::span<const RenderGraphBufferSlot> slots : bufferWriteSlotGroups) {
                for (const RenderGraphBufferSlot& slot : slots) {
                    if (slot.buffer.index < buffers.size() &&
                        buffers[slot.buffer.index].lifetime ==
                            RenderGraphBufferLifetime::Imported) {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] bool passCanBeCulled(std::span<const RenderGraphImageDesc> images,
                                           std::span<const RenderGraphBufferDesc> buffers,
                                           const Pass& pass,
                                           const RenderGraphSchemaRegistry* schemaRegistry) {
            return passAllowsCulling(pass, schemaRegistry) &&
                   !passHasSideEffects(pass, schemaRegistry) &&
                   !passWritesImportedResource(images, buffers, pass);
        }

    } // namespace

    std::vector<bool> findActivePasses(std::span<const Pass> passes,
                                       std::span<const RenderGraphImageDesc> images,
                                       std::span<const RenderGraphBufferDesc> buffers,
                                       std::span<const RenderGraphPassDependency> dependencies,
                                       const RenderGraphSchemaRegistry* schemaRegistry) {
        std::vector<bool> activePasses(passes.size());
        for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
            if (!passCanBeCulled(images, buffers, passes[passIndex], schemaRegistry)) {
                activePasses[passIndex] = true;
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.toDeclarationIndex >= activePasses.size() ||
                    dependency.fromDeclarationIndex >= activePasses.size()) {
                    continue;
                }
                if (activePasses[dependency.toDeclarationIndex] &&
                    !activePasses[dependency.fromDeclarationIndex]) {
                    activePasses[dependency.fromDeclarationIndex] = true;
                    changed = true;
                }
            }
        }

        return activePasses;
    }

} // namespace asharia::rendergraph_internal

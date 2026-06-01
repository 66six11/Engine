#include "render_graph_pass_queries.hpp"

#include <algorithm>
#include <span>
#include <vector>

#include "render_graph_pass.hpp"
#include "render_graph_schema_queries.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] const RenderGraphPassSchema*
        passSchema(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) {
            if (schemaRegistry == nullptr || pass.type.empty()) {
                return nullptr;
            }

            return findPassSchema(pass.type, schemaRegistry);
        }

        [[nodiscard]] bool slotsUseImage(std::span<const RenderGraphImageSlot> slots,
                                         RenderGraphImageHandle image) {
            return std::ranges::any_of(
                slots, [image](const RenderGraphImageSlot& slot) { return slot.image == image; });
        }

        [[nodiscard]] bool slotsUseBuffer(std::span<const RenderGraphBufferSlot> slots,
                                          RenderGraphBufferHandle buffer) {
            return std::ranges::any_of(slots, [buffer](const RenderGraphBufferSlot& slot) {
                return slot.buffer == buffer;
            });
        }

    } // namespace

    bool passAllowsCulling(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) {
        const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
        return pass.allowCulling || (schema != nullptr && schema->allowCulling);
    }

    bool passHasSideEffects(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) {
        const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
        return pass.hasSideEffects || (schema != nullptr && schema->hasSideEffects);
    }

    std::vector<RenderGraphImageHandle> imageHandles(std::span<const RenderGraphImageSlot> slots) {
        std::vector<RenderGraphImageHandle> handles;
        handles.reserve(slots.size());
        for (const RenderGraphImageSlot& slot : slots) {
            handles.push_back(slot.image);
        }
        return handles;
    }

    std::vector<RenderGraphBufferHandle>
    bufferHandles(std::span<const RenderGraphBufferSlot> slots) {
        std::vector<RenderGraphBufferHandle> handles;
        handles.reserve(slots.size());
        for (const RenderGraphBufferSlot& slot : slots) {
            handles.push_back(slot.buffer);
        }
        return handles;
    }

    bool passReadsImage(const Pass& pass, RenderGraphImageHandle image) {
        return slotsUseImage(pass.shaderReadSlots, image) ||
               slotsUseImage(pass.depthReadSlots, image) ||
               slotsUseImage(pass.depthSampledReadSlots, image) ||
               slotsUseImage(pass.transferReadSlots, image);
    }

    bool passWritesImage(const Pass& pass, RenderGraphImageHandle image) {
        return slotsUseImage(pass.colorWriteSlots, image) ||
               slotsUseImage(pass.depthWriteSlots, image) ||
               slotsUseImage(pass.transferWriteSlots, image);
    }

    bool passReadsBuffer(const Pass& pass, RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferTransferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

    bool passWritesBuffer(const Pass& pass, RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferWriteSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

    bool passUsesImage(const RenderGraphCompiledPass& pass, RenderGraphImageHandle image) {
        return slotsUseImage(pass.colorWriteSlots, image) ||
               slotsUseImage(pass.shaderReadSlots, image) ||
               slotsUseImage(pass.depthReadSlots, image) ||
               slotsUseImage(pass.depthWriteSlots, image) ||
               slotsUseImage(pass.depthSampledReadSlots, image) ||
               slotsUseImage(pass.transferReadSlots, image) ||
               slotsUseImage(pass.transferWriteSlots, image);
    }

    bool imageUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                   RenderGraphImageHandle image) {
        return std::ranges::any_of(passes, [image](const RenderGraphCompiledPass& pass) {
            return passUsesImage(pass, image);
        });
    }

    bool passUsesBuffer(const RenderGraphCompiledPass& pass, RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferTransferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferWriteSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

    bool bufferUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                    RenderGraphBufferHandle buffer) {
        return std::ranges::any_of(passes, [buffer](const RenderGraphCompiledPass& pass) {
            return passUsesBuffer(pass, buffer);
        });
    }

    RenderGraphImageTransition makeTransition(RenderGraphImageHandle imageHandle,
                                              const RenderGraphImageDesc& image,
                                              RenderGraphImageAccess oldAccess,
                                              RenderGraphImageAccess newAccess) {
        return RenderGraphImageTransition{
            .image = imageHandle,
            .imageName = image.name,
            .oldState = oldAccess.state,
            .oldShaderStage = oldAccess.shaderStage,
            .newState = newAccess.state,
            .newShaderStage = newAccess.shaderStage,
        };
    }

    RenderGraphBufferTransition makeTransition(RenderGraphBufferHandle bufferHandle,
                                               const RenderGraphBufferDesc& buffer,
                                               RenderGraphBufferAccess oldAccess,
                                               RenderGraphBufferAccess newAccess) {
        return RenderGraphBufferTransition{
            .buffer = bufferHandle,
            .bufferName = buffer.name,
            .oldState = oldAccess.state,
            .oldShaderStage = oldAccess.shaderStage,
            .newState = newAccess.state,
            .newShaderStage = newAccess.shaderStage,
        };
    }

} // namespace asharia::rendergraph_internal

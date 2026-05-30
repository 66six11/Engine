#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

namespace asharia {

    std::vector<RenderGraphImageHandle>
    RenderGraph::Impl::imageHandles(std::span<const RenderGraphImageSlot> slots) {
        std::vector<RenderGraphImageHandle> handles;
        handles.reserve(slots.size());
        for (const RenderGraphImageSlot& slot : slots) {
            handles.push_back(slot.image);
        }
        return handles;
    }

    std::vector<RenderGraphBufferHandle>
    RenderGraph::Impl::bufferHandles(std::span<const RenderGraphBufferSlot> slots) {
        std::vector<RenderGraphBufferHandle> handles;
        handles.reserve(slots.size());
        for (const RenderGraphBufferSlot& slot : slots) {
            handles.push_back(slot.buffer);
        }
        return handles;
    }

    Result<RenderGraphTransientImageAllocation>
    RenderGraph::Impl::makeTransientAllocation(std::size_t imageIndex,
                                               std::span<const RenderGraphCompiledPass> passes,
                                               RenderGraphImageAccess finalAccess) const {
        std::size_t firstPass = passes.size();
        std::size_t lastPass{};
        const RenderGraphImageHandle imageHandle{
            .index = static_cast<std::uint32_t>(imageIndex),
        };

        for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
            if (!passUsesImage(passes[passIndex], imageHandle)) {
                continue;
            }

            if (firstPass == passes.size()) {
                firstPass = passIndex;
            }
            lastPass = passIndex;
        }

        const RenderGraphImageDesc& image = images_[imageIndex];
        if (firstPass == passes.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Transient render graph image '" + image.name + "' is never used.",
            }};
        }

        return RenderGraphTransientImageAllocation{
            .image = imageHandle,
            .imageName = image.name,
            .format = image.format,
            .extent = image.extent,
            .firstPassIndex = firstPass,
            .lastPassIndex = lastPass,
            .finalState = finalAccess.state,
            .finalShaderStage = finalAccess.shaderStage,
        };
    }

    Result<RenderGraphTransientBufferAllocation> RenderGraph::Impl::makeTransientBufferAllocation(
        std::size_t bufferIndex, std::span<const RenderGraphCompiledPass> passes,
        RenderGraphBufferAccess finalAccess) const {
        std::size_t firstPass = passes.size();
        std::size_t lastPass{};
        const RenderGraphBufferHandle bufferHandle{
            .index = static_cast<std::uint32_t>(bufferIndex),
        };

        for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
            if (!passUsesBuffer(passes[passIndex], bufferHandle)) {
                continue;
            }

            if (firstPass == passes.size()) {
                firstPass = passIndex;
            }
            lastPass = passIndex;
        }

        const RenderGraphBufferDesc& buffer = buffers_[bufferIndex];
        if (firstPass == passes.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Transient render graph buffer '" + buffer.name + "' is never used.",
            }};
        }

        return RenderGraphTransientBufferAllocation{
            .buffer = bufferHandle,
            .bufferName = buffer.name,
            .byteSize = buffer.byteSize,
            .firstPassIndex = firstPass,
            .lastPassIndex = lastPass,
            .finalState = finalAccess.state,
            .finalShaderStage = finalAccess.shaderStage,
        };
    }

    bool RenderGraph::Impl::passUsesImage(const RenderGraphCompiledPass& pass,
                                          RenderGraphImageHandle image) {
        return slotsUseImage(pass.colorWriteSlots, image) ||
               slotsUseImage(pass.shaderReadSlots, image) ||
               slotsUseImage(pass.depthReadSlots, image) ||
               slotsUseImage(pass.depthWriteSlots, image) ||
               slotsUseImage(pass.depthSampledReadSlots, image) ||
               slotsUseImage(pass.transferReadSlots, image) ||
               slotsUseImage(pass.transferWriteSlots, image);
    }

    bool
    RenderGraph::Impl::imageUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                                 RenderGraphImageHandle image) {
        return std::ranges::any_of(passes, [image](const RenderGraphCompiledPass& pass) {
            return passUsesImage(pass, image);
        });
    }

    bool RenderGraph::Impl::passUsesBuffer(const RenderGraphCompiledPass& pass,
                                           RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferTransferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferWriteSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

    bool
    RenderGraph::Impl::bufferUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                                  RenderGraphBufferHandle buffer) {
        return std::ranges::any_of(passes, [buffer](const RenderGraphCompiledPass& pass) {
            return passUsesBuffer(pass, buffer);
        });
    }

    bool RenderGraph::Impl::bufferUsedByDeclaredPasses(RenderGraphBufferHandle buffer) const {
        return std::ranges::any_of(passes_, [buffer](const Pass& pass) {
            return passReadsBuffer(pass, buffer) || passWritesBuffer(pass, buffer);
        });
    }

    bool RenderGraph::Impl::imageUsedByDeclaredPasses(RenderGraphImageHandle image) const {
        return std::ranges::any_of(passes_, [image](const Pass& pass) {
            return passReadsImage(pass, image) || passWritesImage(pass, image);
        });
    }

    bool RenderGraph::Impl::slotsUseImage(std::span<const RenderGraphImageSlot> slots,
                                          RenderGraphImageHandle image) {
        return std::ranges::any_of(
            slots, [image](const RenderGraphImageSlot& slot) { return slot.image == image; });
    }

    bool RenderGraph::Impl::slotsUseBuffer(std::span<const RenderGraphBufferSlot> slots,
                                           RenderGraphBufferHandle buffer) {
        return std::ranges::any_of(
            slots, [buffer](const RenderGraphBufferSlot& slot) { return slot.buffer == buffer; });
    }

    Result<void>
    RenderGraph::Impl::transitionImages(std::span<const RenderGraphImageSlot> imageSlots,
                                        RenderGraphImageAccess requiredAccess,
                                        std::vector<RenderGraphImageAccess>& currentAccesses,
                                        RenderGraphCompiledPass& compiledPass) const {
        for (const RenderGraphImageSlot& slot : imageSlots) {
            RenderGraphImageHandle imageHandle = slot.image;
            auto validated = validateImageHandle(imageHandle);
            if (!validated) {
                return std::unexpected{std::move(validated.error())};
            }

            RenderGraphImageAccess slotAccess = requiredAccess;
            if (slotAccess.state == RenderGraphImageState::ShaderRead ||
                slotAccess.state == RenderGraphImageState::DepthSampledRead) {
                slotAccess.shaderStage = slot.shaderStage;
            }

            const RenderGraphImageDesc& image = images_[imageHandle.index];
            if (currentAccesses[imageHandle.index] != slotAccess) {
                compiledPass.transitionsBefore.push_back(makeTransition(
                    imageHandle, image, currentAccesses[imageHandle.index], slotAccess));
                currentAccesses[imageHandle.index] = slotAccess;
            }
        }

        return {};
    }

    Result<void>
    RenderGraph::Impl::transitionBuffers(std::span<const RenderGraphBufferSlot> bufferSlots,
                                         RenderGraphBufferAccess requiredAccess,
                                         std::vector<RenderGraphBufferAccess>& currentAccesses,
                                         RenderGraphCompiledPass& compiledPass) const {
        for (const RenderGraphBufferSlot& slot : bufferSlots) {
            RenderGraphBufferHandle bufferHandle = slot.buffer;
            auto validated = validateBufferHandle(bufferHandle);
            if (!validated) {
                return std::unexpected{std::move(validated.error())};
            }

            RenderGraphBufferAccess slotAccess = requiredAccess;
            if (slotAccess.state == RenderGraphBufferState::ShaderRead ||
                slotAccess.state == RenderGraphBufferState::StorageReadWrite) {
                slotAccess.shaderStage = slot.shaderStage;
            }

            const RenderGraphBufferDesc& buffer = buffers_[bufferHandle.index];
            if (currentAccesses[bufferHandle.index] != slotAccess) {
                compiledPass.bufferTransitionsBefore.push_back(makeTransition(
                    bufferHandle, buffer, currentAccesses[bufferHandle.index], slotAccess));
                currentAccesses[bufferHandle.index] = slotAccess;
            }
        }

        return {};
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RenderGraphImageTransition RenderGraph::Impl::makeTransition(RenderGraphImageHandle imageHandle,
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

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RenderGraphBufferTransition RenderGraph::Impl::makeTransition(
        RenderGraphBufferHandle bufferHandle, const RenderGraphBufferDesc& buffer,
        RenderGraphBufferAccess oldAccess, RenderGraphBufferAccess newAccess) {
        return RenderGraphBufferTransition{
            .buffer = bufferHandle,
            .bufferName = buffer.name,
            .oldState = oldAccess.state,
            .oldShaderStage = oldAccess.shaderStage,
            .newState = newAccess.state,
            .newShaderStage = newAccess.shaderStage,
        };
    }

} // namespace asharia

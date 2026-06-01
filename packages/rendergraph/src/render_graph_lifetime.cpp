#include "render_graph_lifetime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "render_graph_pass_queries.hpp"
#include "render_graph_validation.hpp"

namespace asharia::rendergraph_internal {

    Result<RenderGraphTransientImageAllocation>
    makeTransientAllocation(std::span<const RenderGraphImageDesc> images, std::size_t imageIndex,
                            std::span<const RenderGraphCompiledPass> passes,
                            RenderGraphImageAccess finalAccess) {
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

        const RenderGraphImageDesc& image = images[imageIndex];
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

    Result<RenderGraphTransientBufferAllocation> makeTransientBufferAllocation(
        std::span<const RenderGraphBufferDesc> buffers, std::size_t bufferIndex,
        std::span<const RenderGraphCompiledPass> passes, RenderGraphBufferAccess finalAccess) {
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

        const RenderGraphBufferDesc& buffer = buffers[bufferIndex];
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

    bool bufferUsedByDeclaredPasses(std::span<const Pass> passes, RenderGraphBufferHandle buffer) {
        return std::ranges::any_of(passes, [buffer](const auto& pass) {
            return passReadsBuffer(pass, buffer) || passWritesBuffer(pass, buffer);
        });
    }

    bool imageUsedByDeclaredPasses(std::span<const Pass> passes, RenderGraphImageHandle image) {
        return std::ranges::any_of(passes, [image](const auto& pass) {
            return passReadsImage(pass, image) || passWritesImage(pass, image);
        });
    }

    Result<void> transitionImages(std::span<const RenderGraphImageDesc> images,
                                  std::span<const RenderGraphImageSlot> imageSlots,
                                  RenderGraphImageAccess requiredAccess,
                                  std::vector<RenderGraphImageAccess>& currentAccesses,
                                  RenderGraphCompiledPass& compiledPass) {
        for (const RenderGraphImageSlot& slot : imageSlots) {
            RenderGraphImageHandle imageHandle = slot.image;
            auto validated = validateImageHandle(images, imageHandle);
            if (!validated) {
                return std::unexpected{std::move(validated.error())};
            }

            RenderGraphImageAccess slotAccess = requiredAccess;
            if (slotAccess.state == RenderGraphImageState::ShaderRead ||
                slotAccess.state == RenderGraphImageState::DepthSampledRead) {
                slotAccess.shaderStage = slot.shaderStage;
            }

            const RenderGraphImageDesc& image = images[imageHandle.index];
            if (currentAccesses[imageHandle.index] != slotAccess) {
                compiledPass.transitionsBefore.push_back(makeTransition(
                    imageHandle, image, currentAccesses[imageHandle.index], slotAccess));
                currentAccesses[imageHandle.index] = slotAccess;
            }
        }

        return {};
    }

    Result<void> transitionBuffers(std::span<const RenderGraphBufferDesc> buffers,
                                   std::span<const RenderGraphBufferSlot> bufferSlots,
                                   RenderGraphBufferAccess requiredAccess,
                                   std::vector<RenderGraphBufferAccess>& currentAccesses,
                                   RenderGraphCompiledPass& compiledPass) {
        for (const RenderGraphBufferSlot& slot : bufferSlots) {
            RenderGraphBufferHandle bufferHandle = slot.buffer;
            auto validated = validateBufferHandle(buffers, bufferHandle);
            if (!validated) {
                return std::unexpected{std::move(validated.error())};
            }

            RenderGraphBufferAccess slotAccess = requiredAccess;
            if (slotAccess.state == RenderGraphBufferState::ShaderRead ||
                slotAccess.state == RenderGraphBufferState::StorageReadWrite) {
                slotAccess.shaderStage = slot.shaderStage;
            }

            const RenderGraphBufferDesc& buffer = buffers[bufferHandle.index];
            if (currentAccesses[bufferHandle.index] != slotAccess) {
                compiledPass.bufferTransitionsBefore.push_back(makeTransition(
                    bufferHandle, buffer, currentAccesses[bufferHandle.index], slotAccess));
                currentAccesses[bufferHandle.index] = slotAccess;
            }
        }

        return {};
    }

} // namespace asharia::rendergraph_internal

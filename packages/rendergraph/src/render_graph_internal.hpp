#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_builder.hpp"
#include "asharia/rendergraph/render_graph_command_list.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"
#include "asharia/rendergraph/render_graph_diagnostics.hpp"
#include "asharia/rendergraph/render_graph_execution.hpp"

namespace asharia {

    struct RenderGraph::Impl {
        struct Pass {
            std::string name;
            std::string type;
            std::string paramsType;
            std::vector<std::byte> paramsData;
            std::vector<RenderGraphImageSlot> colorWriteSlots;
            std::vector<RenderGraphImageSlot> shaderReadSlots;
            std::vector<RenderGraphImageSlot> depthReadSlots;
            std::vector<RenderGraphImageSlot> depthWriteSlots;
            std::vector<RenderGraphImageSlot> depthSampledReadSlots;
            std::vector<RenderGraphImageSlot> transferReadSlots;
            std::vector<RenderGraphImageSlot> transferWriteSlots;
            std::vector<RenderGraphBufferSlot> bufferReadSlots;
            std::vector<RenderGraphBufferSlot> bufferTransferReadSlots;
            std::vector<RenderGraphBufferSlot> bufferWriteSlots;
            std::vector<RenderGraphBufferSlot> bufferStorageReadWriteSlots;
            std::vector<RenderGraphCommand> commands;
            bool allowCulling{};
            bool hasSideEffects{};
            RenderGraphPassCallback callback;
        };

        struct ImageSlotGroup {
            std::string_view access;
            std::span<const RenderGraphImageSlot> slots;
        };

        struct BufferSlotGroup {
            std::string_view access;
            std::span<const RenderGraphBufferSlot> slots;
        };

        [[nodiscard]] Result<RenderGraphCompileResult>
        compile(const RenderGraphSchemaRegistry* schemaRegistry) const;

        [[nodiscard]] Result<void>
        execute(const RenderGraphCompileResult& compiled,
                const RenderGraphExecutorRegistry* executorRegistry) const;

        [[nodiscard]] RenderGraphDiagnosticsSnapshot
        diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const;

        [[nodiscard]] std::string formatDebugTables(const RenderGraphCompileResult& compiled) const;

        [[nodiscard]] Result<void> validateImages() const;
        [[nodiscard]] Result<void> validateBuffers() const;

        [[nodiscard]] Result<void>
        validatePass(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) const;

        [[nodiscard]] std::vector<bool>
        findActivePasses(std::span<const RenderGraphPassDependency> dependencies,
                         const RenderGraphSchemaRegistry* schemaRegistry) const;

        [[nodiscard]] std::vector<RenderGraphCulledPass>
        makeCulledPasses(const std::vector<bool>& activePasses) const;

        [[nodiscard]] Result<std::vector<RenderGraphPassDependency>> buildDependencies() const;
        [[nodiscard]] Result<void>
        buildImageDependencies(std::vector<RenderGraphPassDependency>& dependencies) const;
        [[nodiscard]] Result<void>
        buildBufferDependencies(std::vector<RenderGraphPassDependency>& dependencies) const;

        [[nodiscard]] Result<void>
        addReadDependencies(std::vector<RenderGraphPassDependency>& dependencies,
                            RenderGraphImageHandle image, std::span<const std::size_t> writers,
                            std::span<const std::size_t> readers) const;

        [[nodiscard]] Result<void> addBufferReadDependencies(
            std::vector<RenderGraphPassDependency>& dependencies, RenderGraphBufferHandle buffer,
            std::span<const std::size_t> writers, std::span<const std::size_t> readers) const;

        void addDependency(std::vector<RenderGraphPassDependency>& dependencies,
                           std::size_t fromPassIndex, std::size_t toPassIndex,
                           RenderGraphImageHandle image, std::string reason) const;

        void addBufferDependency(std::vector<RenderGraphPassDependency>& dependencies,
                                 std::size_t fromPassIndex, std::size_t toPassIndex,
                                 RenderGraphBufferHandle buffer, std::string reason) const;

        [[nodiscard]] Result<std::vector<std::size_t>>
        sortPassesByDependencies(std::span<const RenderGraphPassDependency> dependencies,
                                 const std::vector<bool>& activePasses) const;

        [[nodiscard]] std::string
        dependencyCycleMessage(std::span<const RenderGraphPassDependency> dependencies,
                               const std::vector<bool>& activePasses,
                               const std::vector<bool>& emitted,
                               const std::vector<std::vector<std::size_t>>& adjacency) const;

        [[nodiscard]] bool
        findDependencyCycleEdge(const std::vector<std::vector<std::size_t>>& adjacency,
                                const std::vector<bool>& activePasses,
                                const std::vector<bool>& emitted, std::size_t& cycleFrom,
                                std::size_t& cycleTo) const;

        [[nodiscard]] std::string missingProducerMessage(std::size_t reader,
                                                         RenderGraphImageHandle image) const;

        [[nodiscard]] std::string
        ambiguousProducerMessage(std::size_t reader, RenderGraphImageHandle image,
                                 std::span<const std::size_t> writers) const;

        [[nodiscard]] std::string
        missingBufferProducerMessage(std::size_t reader, RenderGraphBufferHandle buffer) const;

        [[nodiscard]] std::string
        ambiguousBufferProducerMessage(std::size_t reader, RenderGraphBufferHandle buffer,
                                       std::span<const std::size_t> writers) const;

        [[nodiscard]] bool passCanBeCulled(const Pass& pass,
                                           const RenderGraphSchemaRegistry* schemaRegistry) const;

        [[nodiscard]] static bool
        passAllowsCulling(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry);

        [[nodiscard]] static bool
        passHasSideEffects(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry);

        [[nodiscard]] static const RenderGraphPassSchema*
        passSchema(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry);

        [[nodiscard]] bool passWritesImportedResource(const Pass& pass) const;

        [[nodiscard]] static bool passReadsImage(const Pass& pass, RenderGraphImageHandle image);

        [[nodiscard]] static bool passWritesImage(const Pass& pass, RenderGraphImageHandle image);

        [[nodiscard]] static bool passReadsBuffer(const Pass& pass, RenderGraphBufferHandle buffer);

        [[nodiscard]] static bool passWritesBuffer(const Pass& pass,
                                                   RenderGraphBufferHandle buffer);

        [[nodiscard]] Result<void> validateImageHandle(RenderGraphImageHandle image) const;

        [[nodiscard]] Result<void> validateBufferHandle(RenderGraphBufferHandle buffer) const;

        [[nodiscard]] Result<void> validateWriteSlots(const Pass& pass) const;

        [[nodiscard]] Result<void> validateUniqueImageAccesses(
            const Pass& pass,
            std::span<const std::span<const RenderGraphImageSlot>> slotGroups) const;

        [[nodiscard]] Result<void> validateUniqueBufferAccesses(const Pass& pass) const;

        [[nodiscard]] static Result<void>
        validateSchema(const Pass& pass, const RenderGraphSchemaRegistry& schemaRegistry);

        [[nodiscard]] static Result<void>
        validateCommandsAgainstSchema(const Pass& pass, const RenderGraphPassSchema& schema);

        [[nodiscard]] static Result<void>
        validateSlotsAgainstSchema(const Pass& pass, std::span<const RenderGraphImageSlot> slots,
                                   RenderGraphSlotAccess access,
                                   const RenderGraphPassSchema& schema);

        [[nodiscard]] static Result<void>
        validateSlotsAgainstSchema(const Pass& pass, std::span<const RenderGraphBufferSlot> slots,
                                   RenderGraphSlotAccess access,
                                   const RenderGraphPassSchema& schema);

        [[nodiscard]] static bool hasSlot(const Pass& pass,
                                          const RenderGraphResourceSlotSchema& slotSchema);

        [[nodiscard]] static const RenderGraphResourceSlotSchema*
        findSlotSchema(const RenderGraphPassSchema& schema, std::string_view name,
                       RenderGraphSlotAccess access, RenderGraphShaderStage shaderStage);

        [[nodiscard]] static std::span<const RenderGraphImageSlot>
        slotsForAccess(const Pass& pass, RenderGraphSlotAccess access);

        [[nodiscard]] static std::span<const RenderGraphBufferSlot>
        bufferSlotsForAccess(const Pass& pass, RenderGraphSlotAccess access);

        [[nodiscard]] Result<void> validateSlots(const Pass& pass,
                                                 std::span<const RenderGraphImageSlot> slots) const;

        [[nodiscard]] Result<void>
        validateSlots(const Pass& pass, std::span<const RenderGraphBufferSlot> slots) const;

        [[nodiscard]] Result<void> validateShaderReadSlots(const Pass& pass) const;

        [[nodiscard]] Result<void> validateDepthSampledReadSlots(const Pass& pass) const;

        [[nodiscard]] Result<void> validateBufferReadSlots(const Pass& pass) const;

        [[nodiscard]] Result<void> validateBufferStorageReadWriteSlots(const Pass& pass) const;

        [[nodiscard]] static Result<void> validateUniqueResourceSlotNames(const Pass& pass);

        [[nodiscard]] static std::vector<RenderGraphImageHandle>
        imageHandles(std::span<const RenderGraphImageSlot> slots);

        [[nodiscard]] static std::vector<RenderGraphBufferHandle>
        bufferHandles(std::span<const RenderGraphBufferSlot> slots);

        [[nodiscard]] Result<RenderGraphTransientImageAllocation>
        makeTransientAllocation(std::size_t imageIndex,
                                std::span<const RenderGraphCompiledPass> passes,
                                RenderGraphImageAccess finalAccess) const;

        [[nodiscard]] Result<RenderGraphTransientBufferAllocation>
        makeTransientBufferAllocation(std::size_t bufferIndex,
                                      std::span<const RenderGraphCompiledPass> passes,
                                      RenderGraphBufferAccess finalAccess) const;

        [[nodiscard]] static bool passUsesImage(const RenderGraphCompiledPass& pass,
                                                RenderGraphImageHandle image);

        [[nodiscard]] static bool
        imageUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                  RenderGraphImageHandle image);

        [[nodiscard]] static bool passUsesBuffer(const RenderGraphCompiledPass& pass,
                                                 RenderGraphBufferHandle buffer);

        [[nodiscard]] static bool
        bufferUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                   RenderGraphBufferHandle buffer);

        [[nodiscard]] bool bufferUsedByDeclaredPasses(RenderGraphBufferHandle buffer) const;

        [[nodiscard]] bool imageUsedByDeclaredPasses(RenderGraphImageHandle image) const;

        [[nodiscard]] static bool slotsUseImage(std::span<const RenderGraphImageSlot> slots,
                                                RenderGraphImageHandle image);

        [[nodiscard]] static bool slotsUseBuffer(std::span<const RenderGraphBufferSlot> slots,
                                                 RenderGraphBufferHandle buffer);

        [[nodiscard]] Result<void>
        transitionImages(std::span<const RenderGraphImageSlot> imageSlots,
                         RenderGraphImageAccess requiredAccess,
                         std::vector<RenderGraphImageAccess>& currentAccesses,
                         RenderGraphCompiledPass& compiledPass) const;

        [[nodiscard]] Result<void>
        transitionBuffers(std::span<const RenderGraphBufferSlot> bufferSlots,
                          RenderGraphBufferAccess requiredAccess,
                          std::vector<RenderGraphBufferAccess>& currentAccesses,
                          RenderGraphCompiledPass& compiledPass) const;

        [[nodiscard]] static RenderGraphImageTransition
        makeTransition(RenderGraphImageHandle imageHandle, const RenderGraphImageDesc& image,
                       RenderGraphImageAccess oldAccess, RenderGraphImageAccess newAccess);

        [[nodiscard]] static RenderGraphBufferTransition
        makeTransition(RenderGraphBufferHandle bufferHandle, const RenderGraphBufferDesc& buffer,
                       RenderGraphBufferAccess oldAccess, RenderGraphBufferAccess newAccess);

        [[nodiscard]] static std::string_view imageFormatName(RenderGraphImageFormat format);

        [[nodiscard]] static std::string_view imageStateName(RenderGraphImageState state);

        [[nodiscard]] static std::string_view bufferStateName(RenderGraphBufferState state);

        [[nodiscard]] static std::string_view imageLifetimeName(RenderGraphImageLifetime lifetime);

        [[nodiscard]] static std::string_view
        bufferLifetimeName(RenderGraphBufferLifetime lifetime);

        [[nodiscard]] static std::string
        missingCallbackMessage(const RenderGraphCompiledPass& pass);

        [[nodiscard]] static std::string duplicateSlotMessage(const Pass& pass,
                                                              std::string_view slotName);

        [[nodiscard]] std::string imageAccessConflictMessage(const Pass& pass,
                                                             const RenderGraphImageSlot& slot,
                                                             std::string_view access,
                                                             const RenderGraphImageSlot& otherSlot,
                                                             std::string_view otherAccess) const;

        [[nodiscard]] std::string
        bufferAccessConflictMessage(const Pass& pass, const RenderGraphBufferSlot& slot,
                                    std::string_view access, const RenderGraphBufferSlot& otherSlot,
                                    std::string_view otherAccess) const;

        [[nodiscard]] std::string imageHandleLabel(RenderGraphImageHandle image) const;

        [[nodiscard]] std::string bufferHandleLabel(RenderGraphBufferHandle buffer) const;

        [[nodiscard]] std::string
        dependencyResourceLabel(const RenderGraphPassDependency& dependency) const;

        [[nodiscard]] std::string passDeclarationLabel(std::size_t passIndex) const;

        [[nodiscard]] std::string
        passDeclarationList(std::span<const std::size_t> passIndices) const;

        [[nodiscard]] std::string
        imageHandleList(std::span<const RenderGraphImageHandle> images) const;

        [[nodiscard]] static std::string_view shaderStageName(RenderGraphShaderStage stage);

        [[nodiscard]] static std::string_view commandKindName(RenderGraphCommandKind kind);

        [[nodiscard]] static std::string commandDetail(const RenderGraphCommand& command);

        [[nodiscard]] static std::string imageAccessName(RenderGraphImageState state,
                                                         RenderGraphShaderStage shaderStage);

        [[nodiscard]] static std::string bufferAccessName(RenderGraphBufferState state,
                                                          RenderGraphShaderStage shaderStage);

        [[nodiscard]] std::string slotImageLabel(const RenderGraphImageSlot& slot) const;

        [[nodiscard]] std::string slotBufferLabel(const RenderGraphBufferSlot& slot) const;

        [[nodiscard]] std::string imageSlotList(std::span<const RenderGraphImageSlot> slots) const;

        [[nodiscard]] std::string
        bufferSlotList(std::span<const RenderGraphBufferSlot> slots) const;

        void appendSlotRows(std::string& output, std::string_view passName, std::string_view access,
                            std::span<const RenderGraphImageSlot> slots) const;

        void appendBufferSlotRows(std::string& output, std::string_view passName,
                                  std::string_view access,
                                  std::span<const RenderGraphBufferSlot> slots) const;

        static void appendCommandRows(std::string& output, const RenderGraphCompiledPass& pass);

        void appendTransitionRow(std::string& output, std::string_view phase,
                                 std::string_view passName,
                                 const RenderGraphImageTransition& transition) const;

        void appendTransitionRow(std::string& output, std::string_view phase,
                                 std::string_view passName,
                                 const RenderGraphBufferTransition& transition) const;

        std::vector<RenderGraphImageDesc> images_;
        std::vector<RenderGraphBufferDesc> buffers_;
        std::vector<Pass> passes_;
    };

} // namespace asharia

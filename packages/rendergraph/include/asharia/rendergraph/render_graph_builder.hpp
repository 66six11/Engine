#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_command_list.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"
#include "asharia/rendergraph/render_graph_diagnostics.hpp"
#include "asharia/rendergraph/render_graph_pass_context.hpp"

namespace asharia {

    class RenderGraphExecutorRegistry;
    class RenderGraphSchemaRegistry;

    class RenderGraph {
    public:
        RenderGraph();
        ~RenderGraph();
        RenderGraph(const RenderGraph& other);
        RenderGraph& operator=(const RenderGraph& other);
        RenderGraph(RenderGraph&& other) noexcept;
        RenderGraph& operator=(RenderGraph&& other) noexcept;

        class PassBuilder {
        public:
            PassBuilder& writeColor(RenderGraphImageHandle image);
            PassBuilder& writeColor(std::string slotName, RenderGraphImageHandle image);
            PassBuilder& readTexture(std::string slotName, RenderGraphImageHandle image,
                                     RenderGraphShaderStage shaderStage);

            PassBuilder& readDepth(std::string slotName, RenderGraphImageHandle image);
            PassBuilder& writeDepth(std::string slotName, RenderGraphImageHandle image);
            PassBuilder& readDepthTexture(std::string slotName, RenderGraphImageHandle image,
                                          RenderGraphShaderStage shaderStage);

            PassBuilder& readTransfer(std::string slotName, RenderGraphImageHandle image);
            PassBuilder& readTransfer(RenderGraphImageHandle image);
            PassBuilder& writeTransfer(RenderGraphImageHandle image);
            PassBuilder& writeTransfer(std::string slotName, RenderGraphImageHandle image);
            PassBuilder& readBuffer(std::string slotName, RenderGraphBufferHandle buffer,
                                    RenderGraphShaderStage shaderStage);

            PassBuilder& readTransferBuffer(std::string slotName, RenderGraphBufferHandle buffer);
            PassBuilder& writeBuffer(RenderGraphBufferHandle buffer);
            PassBuilder& writeBuffer(std::string slotName, RenderGraphBufferHandle buffer);
            PassBuilder& readWriteStorageBuffer(std::string slotName,
                                                RenderGraphBufferHandle buffer,
                                                RenderGraphShaderStage shaderStage);

            [[nodiscard]] std::string_view name() const;
            [[nodiscard]] std::string_view type() const;

            PassBuilder& allowCulling(bool allow = true);
            PassBuilder& hasSideEffects(bool hasSideEffects = true);
            PassBuilder& setParamsType(std::string paramsType);
            PassBuilder& setParamsData(std::vector<std::byte> paramsData);

            template <typename Params>
            PassBuilder& setParams(std::string paramsType, const Params& params) {
                static_assert(std::is_trivially_copyable_v<Params>,
                              "RenderGraph pass params must be trivially copyable");
                setParamsType(std::move(paramsType));
                std::vector<std::byte> data(sizeof(Params));
                const auto* bytes = reinterpret_cast<const std::byte*>(&params);
                std::copy(bytes, bytes + sizeof(Params), data.begin());
                return setParamsData(std::move(data));
            }

            PassBuilder& execute(RenderGraphPassCallback callback);
            PassBuilder& setCommands(RenderGraphCommandList commands);

            template <typename Recorder> PassBuilder& recordCommands(Recorder&& recorder) {
                RenderGraphCommandList commands;
                std::forward<Recorder>(recorder)(commands);
                return setCommands(std::move(commands));
            }

        private:
            friend class RenderGraph;

            PassBuilder(RenderGraph& graph, std::size_t passIndex);

            RenderGraph* graph_{};
            std::size_t passIndex_{};
        };

        [[nodiscard]] RenderGraphImageHandle importImage(RenderGraphImageDesc desc);
        [[nodiscard]] RenderGraphImageHandle createTransientImage(RenderGraphImageDesc desc);
        [[nodiscard]] RenderGraphBufferHandle importBuffer(RenderGraphBufferDesc desc);
        [[nodiscard]] RenderGraphBufferHandle createTransientBuffer(RenderGraphBufferDesc desc);

        PassBuilder addPass(std::string name);
        PassBuilder addPass(std::string name, std::string type);

        [[nodiscard]] Result<RenderGraphCompileResult> compile() const;
        [[nodiscard]] Result<RenderGraphCompileResult>
        compile(const RenderGraphSchemaRegistry& schemaRegistry) const;

    private:
        struct Impl;

    public:
        [[nodiscard]] Result<void> execute() const;
        [[nodiscard]] Result<void>
        execute(const RenderGraphExecutorRegistry& executorRegistry) const;
        [[nodiscard]] Result<void> execute(const RenderGraphCompileResult& compiled) const;
        [[nodiscard]] Result<void>
        execute(const RenderGraphCompileResult& compiled,
                const RenderGraphExecutorRegistry& executorRegistry) const;

    private:
        std::unique_ptr<Impl> impl_;

    public:
        [[nodiscard]] RenderGraphDiagnosticsSnapshot
        diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const;

        [[nodiscard]] std::string formatDebugTables(const RenderGraphCompileResult& compiled) const;
    };

} // namespace asharia

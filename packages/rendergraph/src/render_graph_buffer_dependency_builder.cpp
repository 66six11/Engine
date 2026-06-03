#include "render_graph_buffer_dependency_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "render_graph_pass_queries.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] bool bufferCanBeReadFromInitialState(const RenderGraphBufferDesc& buffer) {
            return buffer.lifetime == RenderGraphBufferLifetime::Imported &&
                   buffer.initialState != RenderGraphBufferState::Undefined;
        }

        [[nodiscard]] std::string bufferHandleLabel(std::span<const RenderGraphBufferDesc> buffers,
                                                    RenderGraphBufferHandle buffer) {
            std::string label = "#";
            label += std::to_string(buffer.index);
            if (buffer.index < buffers.size() && !buffers[buffer.index].name.empty()) {
                label += " ";
                label += buffers[buffer.index].name;
            }
            return label;
        }

        [[nodiscard]] std::string passDeclarationLabel(std::span<const Pass> passes,
                                                       std::size_t passIndex) {
            std::string label = "#";
            label += std::to_string(passIndex);
            if (passIndex < passes.size() && !passes[passIndex].name.empty()) {
                label += " ";
                label += passes[passIndex].name;
            }
            return label;
        }

        [[nodiscard]] std::string passDeclarationList(std::span<const Pass> passes,
                                                      std::span<const std::size_t> passIndices) {
            if (passIndices.empty()) {
                return "-";
            }

            std::string labels;
            bool first = true;
            for (const std::size_t passIndex : passIndices) {
                if (!first) {
                    labels += ", ";
                }
                first = false;
                labels += passDeclarationLabel(passes, passIndex);
            }
            return labels;
        }

        [[nodiscard]] std::string missingProducerMessage(const DependencyBuildInputs& inputs,
                                                         std::size_t reader,
                                                         RenderGraphBufferHandle buffer) {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(inputs.passes, reader);
            message += "' reads buffer '";
            message += bufferHandleLabel(inputs.buffers, buffer);
            message += "' before any pass writes it. Candidate writers: -.";
            return message;
        }

        [[nodiscard]] std::string ambiguousProducerMessage(const DependencyBuildInputs& inputs,
                                                           std::size_t reader,
                                                           RenderGraphBufferHandle buffer,
                                                           std::span<const std::size_t> writers) {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(inputs.passes, reader);
            message += "' reads buffer '";
            message += bufferHandleLabel(inputs.buffers, buffer);
            message += "' before a unique producing writer can be inferred. Candidate writers: ";
            message += passDeclarationList(inputs.passes, writers);
            message += ".";
            return message;
        }

        void addBufferDependency(const DependencyBuildInputs& inputs,
                                 std::vector<RenderGraphPassDependency>& dependencies,
                                 std::size_t fromPassIndex, std::size_t toPassIndex,
                                 RenderGraphBufferHandle buffer, std::string_view reason) {
            if (fromPassIndex == toPassIndex) {
                return;
            }

            for (RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex == fromPassIndex &&
                    dependency.toDeclarationIndex == toPassIndex &&
                    dependency.resourceKind == RenderGraphResourceKind::Buffer &&
                    dependency.buffer == buffer) {
                    if (dependency.reason.find(reason) == std::string::npos) {
                        dependency.reason += "; ";
                        dependency.reason += reason;
                    }
                    return;
                }
            }

            std::string bufferName;
            if (buffer.index < inputs.buffers.size()) {
                bufferName = inputs.buffers[buffer.index].name;
            }

            dependencies.push_back(RenderGraphPassDependency{
                .fromDeclarationIndex = fromPassIndex,
                .toDeclarationIndex = toPassIndex,
                .resourceKind = RenderGraphResourceKind::Buffer,
                .buffer = buffer,
                .imageName = {},
                .bufferName = std::move(bufferName),
                .reason = std::string{reason},
            });
        }

        // NOLINTBEGIN(readability-function-cognitive-complexity,
        // bugprone-easily-swappable-parameters)
        [[nodiscard]] Result<void> addBufferReadDependencies(
            const DependencyBuildInputs& inputs,
            std::vector<RenderGraphPassDependency>& dependencies, RenderGraphBufferHandle buffer,
            std::span<const std::size_t> writers, std::span<const std::size_t> readers) {
            const RenderGraphBufferDesc& bufferDesc = inputs.buffers[buffer.index];

            for (const std::size_t reader : readers) {
                if (writers.empty()) {
                    if (!bufferCanBeReadFromInitialState(bufferDesc)) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            missingProducerMessage(inputs, reader, buffer),
                        }};
                    }
                    continue;
                }

                std::size_t sourceWriter{};
                bool hasSourceWriter = false;
                for (const std::size_t writer : writers) {
                    if (writer < reader) {
                        sourceWriter = writer;
                        hasSourceWriter = true;
                    }
                }

                if (!hasSourceWriter && bufferCanBeReadFromInitialState(bufferDesc)) {
                    for (const std::size_t writer : writers) {
                        if (writer > reader) {
                            addBufferDependency(inputs, dependencies, reader, writer, buffer,
                                                "initial read before overwrite");
                        }
                    }
                    continue;
                }

                if (!hasSourceWriter) {
                    if (writers.size() == 1 && writers.front() == reader) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            missingProducerMessage(inputs, reader, buffer),
                        }};
                    }
                    if (writers.size() != 1) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            ambiguousProducerMessage(inputs, reader, buffer, writers),
                        }};
                    }
                    sourceWriter = writers.front();
                }

                addBufferDependency(inputs, dependencies, sourceWriter, reader, buffer,
                                    "producer read");

                for (const std::size_t writer : writers) {
                    if (writer > reader && writer != sourceWriter) {
                        addBufferDependency(inputs, dependencies, reader, writer, buffer,
                                            "read before overwrite");
                    }
                }
            }

            return {};
        }
        // NOLINTEND(readability-function-cognitive-complexity,
        // bugprone-easily-swappable-parameters)

    } // namespace

    Result<void> buildBufferDependencies(const DependencyBuildInputs& inputs,
                                         std::vector<RenderGraphPassDependency>& dependencies) {
        for (std::size_t bufferIndex = 0; bufferIndex < inputs.buffers.size(); ++bufferIndex) {
            const RenderGraphBufferHandle bufferHandle{
                .index = static_cast<std::uint32_t>(bufferIndex),
            };
            std::vector<std::size_t> writers;
            std::vector<std::size_t> readers;

            for (std::size_t passIndex = 0; passIndex < inputs.passes.size(); ++passIndex) {
                const Pass& pass = inputs.passes[passIndex];
                if (passWritesBuffer(pass, bufferHandle)) {
                    writers.push_back(passIndex);
                }
                if (passReadsBuffer(pass, bufferHandle)) {
                    readers.push_back(passIndex);
                }
            }

            for (std::size_t writerIndex = 1; writerIndex < writers.size(); ++writerIndex) {
                addBufferDependency(inputs, dependencies, writers[writerIndex - 1],
                                    writers[writerIndex], bufferHandle, "write order");
            }

            auto readDependencies =
                addBufferReadDependencies(inputs, dependencies, bufferHandle, writers, readers);
            if (!readDependencies) {
                return std::unexpected{std::move(readDependencies.error())};
            }
        }

        return {};
    }

} // namespace asharia::rendergraph_internal

#include "render_graph_image_dependency_builder.hpp"

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

        [[nodiscard]] bool imageCanBeReadFromInitialState(const RenderGraphImageDesc& image) {
            return image.lifetime == RenderGraphImageLifetime::Imported &&
                   image.initialState != RenderGraphImageState::Undefined;
        }

        [[nodiscard]] std::string imageHandleLabel(std::span<const RenderGraphImageDesc> images,
                                                   RenderGraphImageHandle image) {
            std::string label = "#";
            label += std::to_string(image.index);
            if (image.index < images.size() && !images[image.index].name.empty()) {
                label += " ";
                label += images[image.index].name;
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
                                                         RenderGraphImageHandle image) {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(inputs.passes, reader);
            message += "' reads image '";
            message += imageHandleLabel(inputs.images, image);
            message += "' before any pass writes it. Candidate writers: -.";
            return message;
        }

        [[nodiscard]] std::string ambiguousProducerMessage(const DependencyBuildInputs& inputs,
                                                           std::size_t reader,
                                                           RenderGraphImageHandle image,
                                                           std::span<const std::size_t> writers) {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(inputs.passes, reader);
            message += "' reads image '";
            message += imageHandleLabel(inputs.images, image);
            message += "' before a unique producing writer can be inferred. Candidate writers: ";
            message += passDeclarationList(inputs.passes, writers);
            message += ".";
            return message;
        }

        void addImageDependency(const DependencyBuildInputs& inputs,
                                std::vector<RenderGraphPassDependency>& dependencies,
                                std::size_t fromPassIndex, std::size_t toPassIndex,
                                RenderGraphImageHandle image, std::string_view reason) {
            if (fromPassIndex == toPassIndex) {
                return;
            }

            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex == fromPassIndex &&
                    dependency.toDeclarationIndex == toPassIndex &&
                    dependency.resourceKind == RenderGraphResourceKind::Image &&
                    dependency.image == image && dependency.reason == reason) {
                    return;
                }
            }

            std::string imageName;
            if (image.index < inputs.images.size()) {
                imageName = inputs.images[image.index].name;
            }

            dependencies.push_back(RenderGraphPassDependency{
                .fromDeclarationIndex = fromPassIndex,
                .toDeclarationIndex = toPassIndex,
                .resourceKind = RenderGraphResourceKind::Image,
                .image = image,
                .imageName = std::move(imageName),
                .bufferName = {},
                .reason = std::string{reason},
            });
        }

        // NOLINTBEGIN(readability-function-cognitive-complexity,
        // bugprone-easily-swappable-parameters)
        [[nodiscard]] Result<void>
        addImageReadDependencies(const DependencyBuildInputs& inputs,
                                 std::vector<RenderGraphPassDependency>& dependencies,
                                 RenderGraphImageHandle image, std::span<const std::size_t> writers,
                                 std::span<const std::size_t> readers) {
            const RenderGraphImageDesc& imageDesc = inputs.images[image.index];

            for (const std::size_t reader : readers) {
                if (writers.empty()) {
                    if (!imageCanBeReadFromInitialState(imageDesc)) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            missingProducerMessage(inputs, reader, image),
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

                if (!hasSourceWriter && imageCanBeReadFromInitialState(imageDesc)) {
                    for (const std::size_t writer : writers) {
                        if (writer > reader) {
                            addImageDependency(inputs, dependencies, reader, writer, image,
                                               "initial read before overwrite");
                        }
                    }
                    continue;
                }

                if (!hasSourceWriter) {
                    if (writers.size() != 1) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            ambiguousProducerMessage(inputs, reader, image, writers),
                        }};
                    }
                    sourceWriter = writers.front();
                }

                addImageDependency(inputs, dependencies, sourceWriter, reader, image,
                                   "producer read");

                for (const std::size_t writer : writers) {
                    if (writer > reader && writer != sourceWriter) {
                        addImageDependency(inputs, dependencies, reader, writer, image,
                                           "read before overwrite");
                    }
                }
            }

            return {};
        }
        // NOLINTEND(readability-function-cognitive-complexity,
        // bugprone-easily-swappable-parameters)

    } // namespace

    Result<void> buildImageDependencies(const DependencyBuildInputs& inputs,
                                        std::vector<RenderGraphPassDependency>& dependencies) {
        for (std::size_t imageIndex = 0; imageIndex < inputs.images.size(); ++imageIndex) {
            const RenderGraphImageHandle imageHandle{
                .index = static_cast<std::uint32_t>(imageIndex),
            };
            std::vector<std::size_t> writers;
            std::vector<std::size_t> readers;

            for (std::size_t passIndex = 0; passIndex < inputs.passes.size(); ++passIndex) {
                const Pass& pass = inputs.passes[passIndex];
                if (passWritesImage(pass, imageHandle)) {
                    writers.push_back(passIndex);
                }
                if (passReadsImage(pass, imageHandle)) {
                    readers.push_back(passIndex);
                }
            }

            for (std::size_t writerIndex = 1; writerIndex < writers.size(); ++writerIndex) {
                addImageDependency(inputs, dependencies, writers[writerIndex - 1],
                                   writers[writerIndex], imageHandle, "write order");
            }

            auto readDependencies =
                addImageReadDependencies(inputs, dependencies, imageHandle, writers, readers);
            if (!readDependencies) {
                return std::unexpected{std::move(readDependencies.error())};
            }
        }

        return {};
    }

} // namespace asharia::rendergraph_internal

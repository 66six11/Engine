#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

namespace asharia {

    namespace {

        std::size_t activePassCount(const std::vector<bool>& activePasses) {
            std::size_t count = 0;
            for (const bool active : activePasses) {
                if (active) {
                    ++count;
                }
            }

            return count;
        }

        bool addTopoEdge(std::vector<std::vector<std::size_t>>& adjacency,
                         std::size_t fromPassIndex, std::size_t toPassIndex) {
            for (const std::size_t existing : adjacency[fromPassIndex]) {
                if (existing == toPassIndex) {
                    return false;
                }
            }

            adjacency[fromPassIndex].push_back(toPassIndex);
            return true;
        }

        const RenderGraphPassDependency*
        findDependencyForEdge(std::span<const RenderGraphPassDependency> dependencies,
                              std::size_t fromPassIndex, std::size_t toPassIndex) {
            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex == fromPassIndex &&
                    dependency.toDeclarationIndex == toPassIndex) {
                    return &dependency;
                }
            }

            return nullptr;
        }

        bool imageCanBeReadFromInitialState(const RenderGraphImageDesc& image) {
            return image.lifetime == RenderGraphImageLifetime::Imported &&
                   image.initialState != RenderGraphImageState::Undefined;
        }

        bool bufferCanBeReadFromInitialState(const RenderGraphBufferDesc& buffer) {
            return buffer.lifetime == RenderGraphBufferLifetime::Imported &&
                   buffer.initialState != RenderGraphBufferState::Undefined;
        }

    } // namespace

    std::vector<bool>
    RenderGraph::Impl::findActivePasses(std::span<const RenderGraphPassDependency> dependencies,
                                        const RenderGraphSchemaRegistry* schemaRegistry) const {
        std::vector<bool> activePasses(passes_.size());
        for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            if (!passCanBeCulled(passes_[passIndex], schemaRegistry)) {
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

    std::vector<RenderGraphCulledPass>
    RenderGraph::Impl::makeCulledPasses(const std::vector<bool>& activePasses) const {
        std::vector<RenderGraphCulledPass> culledPasses;
        for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            if (passIndex < activePasses.size() && activePasses[passIndex]) {
                continue;
            }

            const Pass& pass = passes_[passIndex];
            culledPasses.push_back(RenderGraphCulledPass{
                .declarationIndex = passIndex,
                .name = pass.name,
                .type = pass.type,
                .reason = "cullable pass has no active consumers or side effects",
            });
        }

        return culledPasses;
    }

    // NOLINTBEGIN(readability-function-cognitive-complexity,
    // bugprone-easily-swappable-parameters)
    Result<void> RenderGraph::Impl::addReadDependencies(
        std::vector<RenderGraphPassDependency>& dependencies, RenderGraphImageHandle image,
        std::span<const std::size_t> writers, std::span<const std::size_t> readers) const {
        const RenderGraphImageDesc& imageDesc = images_[image.index];

        for (const std::size_t reader : readers) {
            if (writers.empty()) {
                if (!imageCanBeReadFromInitialState(imageDesc)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        missingProducerMessage(reader, image),
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
                        addDependency(dependencies, reader, writer, image,
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
                        ambiguousProducerMessage(reader, image, writers),
                    }};
                }
                sourceWriter = writers.front();
            }

            addDependency(dependencies, sourceWriter, reader, image, "producer read");

            for (const std::size_t writer : writers) {
                if (writer > reader && writer != sourceWriter) {
                    addDependency(dependencies, reader, writer, image, "read before overwrite");
                }
            }
        }

        return {};
    }

    Result<void> RenderGraph::Impl::addBufferReadDependencies(
        std::vector<RenderGraphPassDependency>& dependencies, RenderGraphBufferHandle buffer,
        std::span<const std::size_t> writers, std::span<const std::size_t> readers) const {
        const RenderGraphBufferDesc& bufferDesc = buffers_[buffer.index];

        for (const std::size_t reader : readers) {
            if (writers.empty()) {
                if (!bufferCanBeReadFromInitialState(bufferDesc)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        missingBufferProducerMessage(reader, buffer),
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
                        addBufferDependency(dependencies, reader, writer, buffer,
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
                        missingBufferProducerMessage(reader, buffer),
                    }};
                }
                if (writers.size() != 1) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        ambiguousBufferProducerMessage(reader, buffer, writers),
                    }};
                }
                sourceWriter = writers.front();
            }

            addBufferDependency(dependencies, sourceWriter, reader, buffer, "producer read");

            for (const std::size_t writer : writers) {
                if (writer > reader && writer != sourceWriter) {
                    addBufferDependency(dependencies, reader, writer, buffer,
                                        "read before overwrite");
                }
            }
        }

        return {};
    }
    // NOLINTEND(readability-function-cognitive-complexity,
    // bugprone-easily-swappable-parameters)

    void RenderGraph::Impl::addDependency(std::vector<RenderGraphPassDependency>& dependencies,
                                          std::size_t fromPassIndex, std::size_t toPassIndex,
                                          RenderGraphImageHandle image, std::string reason) const {
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

        dependencies.push_back(RenderGraphPassDependency{
            .fromDeclarationIndex = fromPassIndex,
            .toDeclarationIndex = toPassIndex,
            .resourceKind = RenderGraphResourceKind::Image,
            .image = image,
            .imageName = images_[image.index].name,
            .bufferName = {},
            .reason = std::move(reason),
        });
    }

    void RenderGraph::Impl::addBufferDependency(
        std::vector<RenderGraphPassDependency>& dependencies, std::size_t fromPassIndex,
        std::size_t toPassIndex, RenderGraphBufferHandle buffer, std::string reason) const {
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

        dependencies.push_back(RenderGraphPassDependency{
            .fromDeclarationIndex = fromPassIndex,
            .toDeclarationIndex = toPassIndex,
            .resourceKind = RenderGraphResourceKind::Buffer,
            .buffer = buffer,
            .imageName = {},
            .bufferName = buffers_[buffer.index].name,
            .reason = std::move(reason),
        });
    }

    Result<std::vector<std::size_t>> RenderGraph::Impl::sortPassesByDependencies(
        std::span<const RenderGraphPassDependency> dependencies,
        const std::vector<bool>& activePasses) const {
        if (activePasses.size() != passes_.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph active pass set does not match the graph.",
            }};
        }

        std::vector<std::vector<std::size_t>> adjacency(passes_.size());
        std::vector<std::size_t> indegrees(passes_.size());

        for (const RenderGraphPassDependency& dependency : dependencies) {
            if (dependency.fromDeclarationIndex >= passes_.size() ||
                dependency.toDeclarationIndex >= passes_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph dependency references a pass outside the graph.",
                }};
            }

            if (!activePasses[dependency.fromDeclarationIndex] ||
                !activePasses[dependency.toDeclarationIndex]) {
                continue;
            }

            if (addTopoEdge(adjacency, dependency.fromDeclarationIndex,
                            dependency.toDeclarationIndex)) {
                ++indegrees[dependency.toDeclarationIndex];
            }
        }

        std::vector<std::size_t> order;
        const std::size_t targetPassCount = activePassCount(activePasses);
        order.reserve(targetPassCount);
        std::vector<bool> emitted(passes_.size());

        while (order.size() < targetPassCount) {
            std::size_t nextPass = passes_.size();
            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                if (activePasses[passIndex] && !emitted[passIndex] && indegrees[passIndex] == 0) {
                    nextPass = passIndex;
                    break;
                }
            }

            if (nextPass == passes_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    dependencyCycleMessage(dependencies, activePasses, emitted, adjacency),
                }};
            }

            emitted[nextPass] = true;
            order.push_back(nextPass);
            for (const std::size_t dependent : adjacency[nextPass]) {
                --indegrees[dependent];
            }
        }

        return order;
    }

    std::string RenderGraph::Impl::dependencyCycleMessage(
        std::span<const RenderGraphPassDependency> dependencies,
        const std::vector<bool>& activePasses, const std::vector<bool>& emitted,
        const std::vector<std::vector<std::size_t>>& adjacency) const {
        std::string message = "Render graph contains a pass dependency cycle.";

        std::size_t cycleFrom = passes_.size();
        std::size_t cycleTo = passes_.size();
        if (findDependencyCycleEdge(adjacency, activePasses, emitted, cycleFrom, cycleTo)) {
            message += " Cycle edge ";
            message += passDeclarationLabel(cycleFrom);
            message += " -> ";
            message += passDeclarationLabel(cycleTo);

            const RenderGraphPassDependency* dependency =
                findDependencyForEdge(dependencies, cycleFrom, cycleTo);
            if (dependency != nullptr) {
                message += " through resource '";
                message += dependencyResourceLabel(*dependency);
                message += "'";
                if (!dependency->reason.empty()) {
                    message += " (";
                    message += dependency->reason;
                    message += ")";
                }
            }

            message += ".";
        }

        message += " Remaining passes: ";
        bool firstRemaining = true;
        for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            if (!activePasses[passIndex] || emitted[passIndex]) {
                continue;
            }
            if (!firstRemaining) {
                message += ", ";
            }
            firstRemaining = false;
            message += passDeclarationLabel(passIndex);
        }
        if (firstRemaining) {
            message += "-";
        }
        message += ".";

        return message;
    }

    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    bool RenderGraph::Impl::findDependencyCycleEdge(
        const std::vector<std::vector<std::size_t>>& adjacency,
        const std::vector<bool>& activePasses, const std::vector<bool>& emitted,
        std::size_t& cycleFrom, std::size_t& cycleTo) const {
        std::vector<std::uint8_t> visitStates(passes_.size());
        std::function<bool(std::size_t)> visit = [&](std::size_t passIndex) {
            visitStates[passIndex] = 1;
            for (const std::size_t dependent : adjacency[passIndex]) {
                if (!activePasses[dependent] || emitted[dependent]) {
                    continue;
                }
                if (visitStates[dependent] == 1) {
                    cycleFrom = passIndex;
                    cycleTo = dependent;
                    return true;
                }
                if (visitStates[dependent] == 0 && visit(dependent)) {
                    return true;
                }
            }

            visitStates[passIndex] = 2;
            return false;
        };

        for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            if (!activePasses[passIndex] || emitted[passIndex] || visitStates[passIndex] != 0) {
                continue;
            }
            if (visit(passIndex)) {
                return true;
            }
        }

        return false;
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)

    std::string RenderGraph::Impl::missingProducerMessage(std::size_t reader,
                                                          RenderGraphImageHandle image) const {
        std::string message = "Render graph pass '";
        message += passDeclarationLabel(reader);
        message += "' reads image '";
        message += imageHandleLabel(image);
        message += "' before any pass writes it. Candidate writers: -.";
        return message;
    }

    std::string
    RenderGraph::Impl::ambiguousProducerMessage(std::size_t reader, RenderGraphImageHandle image,
                                                std::span<const std::size_t> writers) const {
        std::string message = "Render graph pass '";
        message += passDeclarationLabel(reader);
        message += "' reads image '";
        message += imageHandleLabel(image);
        message += "' before a unique producing writer can be inferred. Candidate writers: ";
        message += passDeclarationList(writers);
        message += ".";
        return message;
    }

    std::string
    RenderGraph::Impl::missingBufferProducerMessage(std::size_t reader,
                                                    RenderGraphBufferHandle buffer) const {
        std::string message = "Render graph pass '";
        message += passDeclarationLabel(reader);
        message += "' reads buffer '";
        message += bufferHandleLabel(buffer);
        message += "' before any pass writes it. Candidate writers: -.";
        return message;
    }

    std::string
    RenderGraph::Impl::ambiguousBufferProducerMessage(std::size_t reader,
                                                      RenderGraphBufferHandle buffer,
                                                      std::span<const std::size_t> writers) const {
        std::string message = "Render graph pass '";
        message += passDeclarationLabel(reader);
        message += "' reads buffer '";
        message += bufferHandleLabel(buffer);
        message += "' before a unique producing writer can be inferred. Candidate writers: ";
        message += passDeclarationList(writers);
        message += ".";
        return message;
    }

    bool RenderGraph::Impl::passCanBeCulled(const Pass& pass,
                                            const RenderGraphSchemaRegistry* schemaRegistry) const {
        return passAllowsCulling(pass, schemaRegistry) &&
               !passHasSideEffects(pass, schemaRegistry) && !passWritesImportedResource(pass);
    }

    bool RenderGraph::Impl::passAllowsCulling(const Pass& pass,
                                              const RenderGraphSchemaRegistry* schemaRegistry) {
        const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
        return pass.allowCulling || (schema != nullptr && schema->allowCulling);
    }

    bool RenderGraph::Impl::passHasSideEffects(const Pass& pass,
                                               const RenderGraphSchemaRegistry* schemaRegistry) {
        const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
        return pass.hasSideEffects || (schema != nullptr && schema->hasSideEffects);
    }

    const RenderGraphPassSchema*
    RenderGraph::Impl::passSchema(const Pass& pass,
                                  const RenderGraphSchemaRegistry* schemaRegistry) {
        if (schemaRegistry == nullptr || pass.type.empty()) {
            return nullptr;
        }

        return schemaRegistry->find(pass.type);
    }

    bool RenderGraph::Impl::passWritesImportedResource(const Pass& pass) const {
        const std::array<std::span<const RenderGraphImageSlot>, 3> writeSlotGroups{
            pass.colorWriteSlots,
            pass.depthWriteSlots,
            pass.transferWriteSlots,
        };
        for (std::span<const RenderGraphImageSlot> slots : writeSlotGroups) {
            for (const RenderGraphImageSlot& slot : slots) {
                if (slot.image.index < images_.size() &&
                    images_[slot.image.index].lifetime == RenderGraphImageLifetime::Imported) {
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
                if (slot.buffer.index < buffers_.size() &&
                    buffers_[slot.buffer.index].lifetime == RenderGraphBufferLifetime::Imported) {
                    return true;
                }
            }
        }

        return false;
    }

    bool RenderGraph::Impl::passReadsImage(const Pass& pass, RenderGraphImageHandle image) {
        return slotsUseImage(pass.shaderReadSlots, image) ||
               slotsUseImage(pass.depthReadSlots, image) ||
               slotsUseImage(pass.depthSampledReadSlots, image) ||
               slotsUseImage(pass.transferReadSlots, image);
    }

    bool RenderGraph::Impl::passWritesImage(const Pass& pass, RenderGraphImageHandle image) {
        return slotsUseImage(pass.colorWriteSlots, image) ||
               slotsUseImage(pass.depthWriteSlots, image) ||
               slotsUseImage(pass.transferWriteSlots, image);
    }

    bool RenderGraph::Impl::passReadsBuffer(const Pass& pass, RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferTransferReadSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

    bool RenderGraph::Impl::passWritesBuffer(const Pass& pass, RenderGraphBufferHandle buffer) {
        return slotsUseBuffer(pass.bufferWriteSlots, buffer) ||
               slotsUseBuffer(pass.bufferStorageReadWriteSlots, buffer);
    }

} // namespace asharia

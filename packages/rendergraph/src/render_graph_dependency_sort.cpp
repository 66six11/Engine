#include "render_graph_dependency_sort.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] std::size_t activePassCount(const std::vector<bool>& activePasses) {
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

        [[nodiscard]] const RenderGraphPassDependency*
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

        // NOLINTBEGIN(bugprone-easily-swappable-parameters)
        bool findDependencyCycleEdge(const std::vector<std::vector<std::size_t>>& adjacency,
                                     const std::vector<bool>& activePasses,
                                     const std::vector<bool>& emitted, std::size_t& cycleFrom,
                                     std::size_t& cycleTo) {
            std::vector<std::uint8_t> visitStates(activePasses.size());
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

            for (std::size_t passIndex = 0; passIndex < activePasses.size(); ++passIndex) {
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

        [[nodiscard]] std::string
        dependencyResourceLabel(const RenderGraphPassDependency& dependency) {
            const bool isBuffer = dependency.resourceKind == RenderGraphResourceKind::Buffer;
            std::string label = "#";
            label += std::to_string(isBuffer ? dependency.buffer.index : dependency.image.index);
            const std::string& name = isBuffer ? dependency.bufferName : dependency.imageName;
            if (!name.empty()) {
                label += " ";
                label += name;
            }
            return label;
        }

        [[nodiscard]] std::string dependencyCycleMessage(
            std::span<const Pass> passes, std::span<const RenderGraphPassDependency> dependencies,
            const std::vector<bool>& activePasses, const std::vector<bool>& emitted,
            const std::vector<std::vector<std::size_t>>& adjacency) {
            std::string message = "Render graph contains a pass dependency cycle.";

            std::size_t cycleFrom = activePasses.size();
            std::size_t cycleTo = activePasses.size();
            if (findDependencyCycleEdge(adjacency, activePasses, emitted, cycleFrom, cycleTo)) {
                message += " Cycle edge ";
                message += passDeclarationLabel(passes, cycleFrom);
                message += " -> ";
                message += passDeclarationLabel(passes, cycleTo);

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
            for (std::size_t passIndex = 0; passIndex < activePasses.size(); ++passIndex) {
                if (!activePasses[passIndex] || emitted[passIndex]) {
                    continue;
                }
                if (!firstRemaining) {
                    message += ", ";
                }
                firstRemaining = false;
                message += passDeclarationLabel(passes, passIndex);
            }
            if (firstRemaining) {
                message += "-";
            }
            message += ".";

            return message;
        }

    } // namespace

    Result<std::vector<std::size_t>>
    sortPassesByDependencies(std::span<const Pass> passes,
                             std::span<const RenderGraphPassDependency> dependencies,
                             const std::vector<bool>& activePasses) {
        if (activePasses.size() != passes.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph active pass set does not match the graph.",
            }};
        }

        std::vector<std::vector<std::size_t>> adjacency(passes.size());
        std::vector<std::size_t> indegrees(passes.size());

        for (const RenderGraphPassDependency& dependency : dependencies) {
            if (dependency.fromDeclarationIndex >= passes.size() ||
                dependency.toDeclarationIndex >= passes.size()) {
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
        std::vector<bool> emitted(passes.size());

        while (order.size() < targetPassCount) {
            std::size_t nextPass = passes.size();
            for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
                if (activePasses[passIndex] && !emitted[passIndex] && indegrees[passIndex] == 0) {
                    nextPass = passIndex;
                    break;
                }
            }

            if (nextPass == passes.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    dependencyCycleMessage(passes, dependencies, activePasses, emitted, adjacency),
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

} // namespace asharia::rendergraph_internal

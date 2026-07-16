#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asharia/host_runtime/process_scope.hpp"

namespace asharia::host_runtime {

    struct ProcessScopeDiagnosticAttributionStateV1 final {
        std::string engineGenerationId;
        ExactFactoryReferenceV1 factory;
    };

    struct ResolvedProcessFactoryStateV1 final {
        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV1> attribution;
        std::size_t descriptorIndex{};
        std::vector<std::size_t> dependencyIndices;
        std::vector<FactoryDependencyViewV1> dependencyScratch;
        std::optional<FactoryInstanceTokenV1> instance;
        bool active{};
    };

    struct ProcessScopeExecutorStateV1 final {
        ProcessScopeExecutorStateV1(
            AdmittedStaticFactoryCallbackTableV1 admittedTableValue,
            std::vector<ResolvedProcessFactoryStateV1> factoriesValue,
            std::vector<ProcessScopeLifecycleDiagnosticV1> diagnosticScratchValue) noexcept
            : admittedTable(std::move(admittedTableValue)), factories(std::move(factoriesValue)),
              diagnosticScratch(std::move(diagnosticScratchValue)) {}

        // Keep the admitted owner before token-bearing records so member destruction
        // always tears records down before the exact callback table.
        AdmittedStaticFactoryCallbackTableV1 admittedTable;
        std::vector<ResolvedProcessFactoryStateV1> factories;
        std::vector<ProcessScopeLifecycleDiagnosticV1> diagnosticScratch;
        ProcessScopeStateV1 lifecycleState{ProcessScopeStateV1::Prepared};
        bool operationInProgress{};
    };

    class ProcessScopeStateAccessV1 final {
    public:
        [[nodiscard]] static ProcessScopeExecutorV1
        makeExecutor(std::unique_ptr<ProcessScopeExecutorStateV1> state) noexcept {
            return ProcessScopeExecutorV1{std::move(state)};
        }

        [[nodiscard]] static ProcessScopeLifecycleDiagnosticV1
        makeDiagnostic(std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV1> attribution,
                       ProcessScopeLifecycleStageV1 stage,
                       std::uint32_t providerLocalCode) noexcept {
            return ProcessScopeLifecycleDiagnosticV1{std::move(attribution), stage,
                                                     providerLocalCode};
        }
    };

} // namespace asharia::host_runtime

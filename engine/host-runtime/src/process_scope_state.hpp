#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asharia/host_runtime/process_scope.hpp"

#include "process_contribution_activation_lease.hpp"
#include "process_contribution_registry_state.hpp"

namespace asharia::host_runtime {

    struct ProcessScopeDiagnosticAttributionStateV2 final {
        std::string engineGenerationId;
        ExactFactoryReferenceV2 factory;
    };

    struct ProcessScopeContributionDiagnosticAttributionStateV2 final {
        std::string engineGenerationId;
        ExactFactoryReferenceV2 factory;
        std::string contributionId;
        std::string contributionKind;
    };

    struct ResolvedProcessFactoryStateV2 final {
        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV2> attribution;
        std::size_t descriptorIndex{};
        std::vector<std::size_t> dependencyIndices;
        std::vector<FactoryDependencyViewV1> dependencyScratch;
        std::vector<std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2>>
            contributionAttributions;
        std::optional<FactoryInstanceTokenV1> instance;
        std::optional<ProcessContributionActivationLeaseV1> contributionLease;
        bool lifecycleActivated{};
        bool dependencyVisible{};
    };

    struct ProcessScopeExecutorStateV2 final {
        ProcessScopeExecutorStateV2(
            AdmittedStaticFactoryCallbackTableV2 admittedTableValue,
            std::shared_ptr<ProcessContributionRegistryGenerationStateV1> contributionRegistryValue,
            std::vector<ResolvedProcessFactoryStateV2> factoriesValue,
            std::vector<ProcessScopeLifecycleDiagnosticV2> diagnosticScratchValue) noexcept
            : admittedTable(std::move(admittedTableValue)),
              contributionRegistry(std::move(contributionRegistryValue)),
              factories(std::move(factoriesValue)),
              diagnosticScratch(std::move(diagnosticScratchValue)) {}

        // Keep the admitted owner before token-bearing records so member destruction
        // always tears records down before the exact callback table.
        AdmittedStaticFactoryCallbackTableV2 admittedTable;
        std::shared_ptr<ProcessContributionRegistryGenerationStateV1> contributionRegistry;
        std::vector<ResolvedProcessFactoryStateV2> factories;
        std::vector<ProcessScopeLifecycleDiagnosticV2> diagnosticScratch;
        ProcessScopeStateV2 lifecycleState{ProcessScopeStateV2::Prepared};
        bool operationInProgress{};
    };

    class ProcessScopeStateAccessV2 final {
    public:
        [[nodiscard]] static ProcessScopeExecutorV2
        makeExecutor(std::unique_ptr<ProcessScopeExecutorStateV2> state) noexcept {
            return ProcessScopeExecutorV2{std::move(state)};
        }

        [[nodiscard]] static ProcessScopeLifecycleDiagnosticV2
        makeDiagnostic(std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV2> attribution,
                       ProcessScopeLifecycleStageV2 stage,
                       std::uint32_t providerLocalCode) noexcept {
            return ProcessScopeLifecycleDiagnosticV2{std::move(attribution), stage,
                                                     providerLocalCode};
        }

        [[nodiscard]] static ProcessScopeContributionPublicationDiagnosticV2
        makePublicationDiagnostic(
            std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2> attribution,
            ProcessScopeContributionPublicationStageV2 stage) noexcept {
            return ProcessScopeContributionPublicationDiagnosticV2{std::move(attribution), stage};
        }
    };

} // namespace asharia::host_runtime

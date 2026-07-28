#include "asharia/host_runtime/process_scope.hpp"

#include <exception>
#include <memory>
#include <utility>

#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    ProcessScopeLifecycleDiagnosticV2::ProcessScopeLifecycleDiagnosticV2(
        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV2> attribution,
        ProcessScopeLifecycleStageV2 stage, std::uint32_t providerLocalCode) noexcept
        : attribution_(std::move(attribution)), stage_(stage),
          providerLocalCode_(providerLocalCode) {}

    ProcessScopeLifecycleStageV2 ProcessScopeLifecycleDiagnosticV2::stage() const noexcept {
        return stage_;
    }

    std::string_view ProcessScopeLifecycleDiagnosticV2::engineGenerationId() const noexcept {
        return attribution_ ? std::string_view{attribution_->engineGenerationId}
                            : std::string_view{};
    }

    ExactFactoryReferenceViewV1 ProcessScopeLifecycleDiagnosticV2::factory() const noexcept {
        return attribution_ ? attribution_->factory.view() : ExactFactoryReferenceViewV1{};
    }

    std::uint32_t ProcessScopeLifecycleDiagnosticV2::providerLocalCode() const noexcept {
        return providerLocalCode_;
    }

    ProcessScopeContributionPublicationDiagnosticV2::
        ProcessScopeContributionPublicationDiagnosticV2(
            std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2> attribution,
            ProcessScopeContributionPublicationStageV2 stage) noexcept
        : attribution_(std::move(attribution)), stage_(stage) {}

    ProcessScopeContributionPublicationStageV2
    ProcessScopeContributionPublicationDiagnosticV2::stage() const noexcept {
        return stage_;
    }

    std::string_view
    ProcessScopeContributionPublicationDiagnosticV2::engineGenerationId() const noexcept {
        return attribution_ ? std::string_view{attribution_->engineGenerationId}
                            : std::string_view{};
    }

    ExactFactoryReferenceViewV1
    ProcessScopeContributionPublicationDiagnosticV2::factory() const noexcept {
        return attribution_ ? attribution_->factory.view() : ExactFactoryReferenceViewV1{};
    }

    std::string_view
    ProcessScopeContributionPublicationDiagnosticV2::contributionId() const noexcept {
        return attribution_ ? std::string_view{attribution_->contributionId} : std::string_view{};
    }

    std::string_view
    ProcessScopeContributionPublicationDiagnosticV2::contributionKind() const noexcept {
        return attribution_ ? std::string_view{attribution_->contributionKind} : std::string_view{};
    }

    ProcessScopeExecutorV2::ProcessScopeExecutorV2(
        std::unique_ptr<ProcessScopeExecutorStateV2> state) noexcept
        : state_(std::move(state)) {}

    ProcessScopeExecutorV2::~ProcessScopeExecutorV2() noexcept {
        if (state_ && (state_->operationInProgress ||
                       state_->lifecycleState == ProcessScopeStateV2::Active)) {
            std::terminate();
        }
    }

    ProcessScopeExecutorV2::ProcessScopeExecutorV2(ProcessScopeExecutorV2&& other) noexcept {
        if (other.state_ && other.state_->operationInProgress) {
            std::terminate();
        }
        state_ = std::move(other.state_);
    }

    ProcessScopeStateV2 ProcessScopeExecutorV2::state() const noexcept {
        return state_ ? state_->lifecycleState : ProcessScopeStateV2::MovedFrom;
    }

    std::expected<ProcessContributionRegistryViewV1, ProcessContributionLookupErrorV1>
    ProcessScopeExecutorV2::contributions() const noexcept {
        if (!state_) {
            return std::unexpected(ProcessContributionLookupErrorV1{
                .code = ProcessContributionLookupErrorCodeV1::ExecutorMovedFrom,
            });
        }
        if (state_->operationInProgress) {
            return std::unexpected(ProcessContributionLookupErrorV1{
                .code = ProcessContributionLookupErrorCodeV1::OperationInProgress,
            });
        }
        if (state_->lifecycleState != ProcessScopeStateV2::Active) {
            return std::unexpected(ProcessContributionLookupErrorV1{
                .code = ProcessContributionLookupErrorCodeV1::RegistryNotActive,
            });
        }

        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV2::executionView(state_->admittedTable);
        if (!executionView) {
            ProcessContributionLookupErrorCodeV1 code =
                ProcessContributionLookupErrorCodeV1::RegistryExpired;
            switch (executionView.error()) {
            case AdmittedFactoryExecutionAccessErrorV2::WrongControlThread:
                code = ProcessContributionLookupErrorCodeV1::WrongControlThread;
                break;
            case AdmittedFactoryExecutionAccessErrorV2::ProcessEpochStale:
                code = ProcessContributionLookupErrorCodeV1::ProcessEpochStale;
                break;
            case AdmittedFactoryExecutionAccessErrorV2::MovedFrom:
            case AdmittedFactoryExecutionAccessErrorV2::TableInvalid:
                break;
            }
            return std::unexpected(ProcessContributionLookupErrorV1{.code = code});
        }
        return ProcessContributionRegistryStateAccessV1::view(state_->contributionRegistry);
    }

    ProcessScopeErrorCodeV2
    mapExecutionAccessError(AdmittedFactoryExecutionAccessErrorV2 error) noexcept {
        switch (error) {
        case AdmittedFactoryExecutionAccessErrorV2::MovedFrom:
            return ProcessScopeErrorCodeV2::AdmissionMovedFrom;
        case AdmittedFactoryExecutionAccessErrorV2::WrongControlThread:
            return ProcessScopeErrorCodeV2::WrongControlThread;
        case AdmittedFactoryExecutionAccessErrorV2::ProcessEpochStale:
            return ProcessScopeErrorCodeV2::ProcessEpochStale;
        case AdmittedFactoryExecutionAccessErrorV2::TableInvalid:
            return ProcessScopeErrorCodeV2::AdmittedTableInvalid;
        }
        return ProcessScopeErrorCodeV2::AdmittedTableInvalid;
    }

    ProcessScopeOperationErrorV2
    makeProcessScopeOperationError(ProcessScopeErrorCodeV2 code,
                                   ProcessScopeStateV2 state) noexcept {
        return {
            .code = code,
            .state = state,
        };
    }

    FactoryAttributionViewV1
    processFactoryAttribution(const ResolvedProcessFactoryStateV2& factory) noexcept {
        if (!factory.attribution) {
            return {};
        }
        return {
            .engineGenerationId = factory.attribution->engineGenerationId,
            .factory = factory.attribution->factory.view(),
        };
    }

    ProcessScopeLifecycleDiagnosticV2
    makeProcessScopeDiagnostic(const ResolvedProcessFactoryStateV2& factory,
                               ProcessScopeLifecycleStageV2 stage,
                               std::uint32_t providerLocalCode) noexcept {
        return ProcessScopeStateAccessV2::makeDiagnostic(factory.attribution, stage,
                                                         providerLocalCode);
    }

    void appendProcessScopeCleanupDiagnostic(ProcessScopeExecutorStateV2& state,
                                             const ResolvedProcessFactoryStateV2& factory,
                                             ProcessScopeLifecycleStageV2 stage,
                                             std::uint32_t providerLocalCode) noexcept {
        try {
            state.diagnosticScratch.push_back(
                makeProcessScopeDiagnostic(factory, stage, providerLocalCode));
        } catch (...) {
            // Preflight reserves the exact worst-case capacity. Reaching this path
            // means the execution state no longer satisfies its construction contract.
            std::terminate();
        }
    }

} // namespace asharia::host_runtime

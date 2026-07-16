#include "asharia/host_runtime/process_scope.hpp"

#include <exception>
#include <memory>
#include <utility>

#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    ProcessScopeLifecycleDiagnosticV1::ProcessScopeLifecycleDiagnosticV1(
        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV1> attribution,
        ProcessScopeLifecycleStageV1 stage, std::uint32_t providerLocalCode) noexcept
        : attribution_(std::move(attribution)), stage_(stage),
          providerLocalCode_(providerLocalCode) {}

    ProcessScopeLifecycleStageV1 ProcessScopeLifecycleDiagnosticV1::stage() const noexcept {
        return stage_;
    }

    std::string_view ProcessScopeLifecycleDiagnosticV1::engineGenerationId() const noexcept {
        return attribution_ ? std::string_view{attribution_->engineGenerationId}
                            : std::string_view{};
    }

    ExactFactoryReferenceViewV1 ProcessScopeLifecycleDiagnosticV1::factory() const noexcept {
        return attribution_ ? attribution_->factory.view() : ExactFactoryReferenceViewV1{};
    }

    std::uint32_t ProcessScopeLifecycleDiagnosticV1::providerLocalCode() const noexcept {
        return providerLocalCode_;
    }

    ProcessScopeExecutorV1::ProcessScopeExecutorV1(
        std::unique_ptr<ProcessScopeExecutorStateV1> state) noexcept
        : state_(std::move(state)) {}

    ProcessScopeExecutorV1::~ProcessScopeExecutorV1() noexcept {
        if (state_ && (state_->operationInProgress ||
                       state_->lifecycleState == ProcessScopeStateV1::Active)) {
            std::terminate();
        }
    }

    ProcessScopeExecutorV1::ProcessScopeExecutorV1(ProcessScopeExecutorV1&& other) noexcept {
        if (other.state_ && other.state_->operationInProgress) {
            std::terminate();
        }
        state_ = std::move(other.state_);
    }

    ProcessScopeStateV1 ProcessScopeExecutorV1::state() const noexcept {
        return state_ ? state_->lifecycleState : ProcessScopeStateV1::MovedFrom;
    }

    ProcessScopeErrorCodeV1
    mapExecutionAccessError(AdmittedFactoryExecutionAccessErrorV1 error) noexcept {
        switch (error) {
        case AdmittedFactoryExecutionAccessErrorV1::MovedFrom:
            return ProcessScopeErrorCodeV1::AdmissionMovedFrom;
        case AdmittedFactoryExecutionAccessErrorV1::WrongControlThread:
            return ProcessScopeErrorCodeV1::WrongControlThread;
        case AdmittedFactoryExecutionAccessErrorV1::ProcessEpochStale:
            return ProcessScopeErrorCodeV1::ProcessEpochStale;
        case AdmittedFactoryExecutionAccessErrorV1::TableInvalid:
            return ProcessScopeErrorCodeV1::AdmittedTableInvalid;
        }
        return ProcessScopeErrorCodeV1::AdmittedTableInvalid;
    }

    ProcessScopeOperationErrorV1
    makeProcessScopeOperationError(ProcessScopeErrorCodeV1 code,
                                   ProcessScopeStateV1 state) noexcept {
        return {
            .code = code,
            .state = state,
        };
    }

    FactoryAttributionViewV1
    processFactoryAttribution(const ResolvedProcessFactoryStateV1& factory) noexcept {
        if (!factory.attribution) {
            return {};
        }
        return {
            .engineGenerationId = factory.attribution->engineGenerationId,
            .factory = factory.attribution->factory.view(),
        };
    }

    ProcessScopeLifecycleDiagnosticV1
    makeProcessScopeDiagnostic(const ResolvedProcessFactoryStateV1& factory,
                               ProcessScopeLifecycleStageV1 stage,
                               std::uint32_t providerLocalCode) noexcept {
        return ProcessScopeStateAccessV1::makeDiagnostic(factory.attribution, stage,
                                                         providerLocalCode);
    }

    void appendProcessScopeCleanupDiagnostic(ProcessScopeExecutorStateV1& state,
                                             const ResolvedProcessFactoryStateV1& factory,
                                             ProcessScopeLifecycleStageV1 stage,
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

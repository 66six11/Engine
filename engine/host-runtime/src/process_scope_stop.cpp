#include <utility>

#include "asharia/host_runtime/process_scope.hpp"

#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    ProcessScopeStopResultV1 ProcessScopeExecutorV1::stop() noexcept {
        if (!state_) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV1::ExecutorMovedFrom, ProcessScopeStateV1::MovedFrom));
        }
        if (state_->operationInProgress) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV1::OperationInProgress, state_->lifecycleState));
        }
        if (state_->lifecycleState != ProcessScopeStateV1::Active) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV1::StopRequiresActive, state_->lifecycleState));
        }

        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV1::executionView(state_->admittedTable);
        if (!executionView) {
            return std::unexpected(makeProcessScopeOperationError(
                mapExecutionAccessError(executionView.error()), state_->lifecycleState));
        }

        state_->diagnosticScratch.clear();
        ProcessScopeOperationGuardV1 operationGuard{state_->operationInProgress};
        cleanupProcessScopeFactories(*state_, executionView->callbacks);
        state_->lifecycleState = ProcessScopeStateV1::Stopped;
        return ProcessScopeStopReportV1{
            .cleanupDiagnostics = std::move(state_->diagnosticScratch),
        };
    }

} // namespace asharia::host_runtime

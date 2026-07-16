#include <utility>

#include "asharia/host_runtime/process_scope.hpp"

#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    ProcessScopeStopResultV2 ProcessScopeExecutorV2::stop() noexcept {
        if (!state_) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV2::ExecutorMovedFrom, ProcessScopeStateV2::MovedFrom));
        }
        if (state_->operationInProgress) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV2::OperationInProgress, state_->lifecycleState));
        }
        if (state_->lifecycleState != ProcessScopeStateV2::Active) {
            return std::unexpected(makeProcessScopeOperationError(
                ProcessScopeErrorCodeV2::StopRequiresActive, state_->lifecycleState));
        }

        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV1::executionView(state_->admittedTable);
        if (!executionView) {
            return std::unexpected(makeProcessScopeOperationError(
                mapExecutionAccessError(executionView.error()), state_->lifecycleState));
        }

        state_->diagnosticScratch.clear();
        ProcessScopeOperationGuardV2 operationGuard{state_->operationInProgress};
        cleanupProcessScopeFactories(*state_, executionView->callbacks);
        state_->lifecycleState = ProcessScopeStateV2::Stopped;
        return ProcessScopeStopReportV2{
            .cleanupDiagnostics = std::move(state_->diagnosticScratch),
        };
    }

} // namespace asharia::host_runtime

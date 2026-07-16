#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <utility>

#include "asharia/host_runtime/process_scope.hpp"

#include "factory_lifecycle_context_access.hpp"
#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {
    namespace {

        void appendDependencyView(ResolvedProcessFactoryStateV1& factory,
                                  ExactFactoryReferenceViewV1 dependencyFactory,
                                  FactoryInstanceViewV1 dependencyInstance) noexcept {
            try {
                factory.dependencyScratch.push_back({
                    .factory = dependencyFactory,
                    .instance = dependencyInstance,
                });
            } catch (...) {
                // Capacity is reserved during preflight; an exception here indicates
                // corrupted execution bookkeeping rather than recoverable OOM.
                std::terminate();
            }
        }

        [[nodiscard]] ProcessScopeStartResultV1
        operationFailure(ProcessScopeErrorCodeV1 code, ProcessScopeStateV1 state) noexcept {
            return std::unexpected(ProcessScopeStartFailureV1{
                .operation = makeProcessScopeOperationError(code, state),
                .primary = std::nullopt,
                .cleanupDiagnostics = {},
            });
        }

        [[nodiscard]] ProcessScopeStartResultV1
        rollbackWithoutPrimary(ProcessScopeExecutorStateV1& state,
                               std::span<const StaticFactoryCallbacksV1> callbacks,
                               ProcessScopeErrorCodeV1 code) noexcept {
            state.diagnosticScratch.clear();
            cleanupProcessScopeFactories(state, callbacks);
            state.lifecycleState = ProcessScopeStateV1::StartFailed;
            return std::unexpected(ProcessScopeStartFailureV1{
                .operation = makeProcessScopeOperationError(code, ProcessScopeStateV1::StartFailed),
                .primary = std::nullopt,
                .cleanupDiagnostics = std::move(state.diagnosticScratch),
            });
        }

        [[nodiscard]] ProcessScopeStartResultV1 rollbackLifecycleFailure(
            ProcessScopeExecutorStateV1& state, std::span<const StaticFactoryCallbacksV1> callbacks,
            const ResolvedProcessFactoryStateV1& failedFactory,
            ProcessScopeLifecycleStageV1 failedStage, ProcessScopeErrorCodeV1 errorCode,
            std::uint32_t providerLocalCode) noexcept {
            ProcessScopeLifecycleDiagnosticV1 primary =
                makeProcessScopeDiagnostic(failedFactory, failedStage, providerLocalCode);
            state.diagnosticScratch.clear();
            cleanupProcessScopeFactories(state, callbacks);
            state.lifecycleState = ProcessScopeStateV1::StartFailed;
            return std::unexpected(ProcessScopeStartFailureV1{
                .operation =
                    makeProcessScopeOperationError(errorCode, ProcessScopeStateV1::StartFailed),
                .primary = std::move(primary),
                .cleanupDiagnostics = std::move(state.diagnosticScratch),
            });
        }

    } // namespace

    ProcessScopeStartResultV1 ProcessScopeExecutorV1::start() noexcept {
        if (!state_) {
            return operationFailure(ProcessScopeErrorCodeV1::ExecutorMovedFrom,
                                    ProcessScopeStateV1::MovedFrom);
        }
        if (state_->operationInProgress) {
            return operationFailure(ProcessScopeErrorCodeV1::OperationInProgress,
                                    state_->lifecycleState);
        }
        if (state_->lifecycleState != ProcessScopeStateV1::Prepared) {
            return operationFailure(ProcessScopeErrorCodeV1::StartRequiresPrepared,
                                    state_->lifecycleState);
        }

        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV1::executionView(state_->admittedTable);
        if (!executionView) {
            const ProcessScopeErrorCodeV1 code = mapExecutionAccessError(executionView.error());
            if (code == ProcessScopeErrorCodeV1::WrongControlThread) {
                return operationFailure(code, state_->lifecycleState);
            }
            state_->lifecycleState = ProcessScopeStateV1::StartFailed;
            return operationFailure(code, state_->lifecycleState);
        }

        const std::span<const StaticFactoryCallbacksV1> callbacks = executionView->callbacks;
        ProcessScopeOperationGuardV1 operationGuard{state_->operationInProgress};
        state_->diagnosticScratch.clear();
        for (ResolvedProcessFactoryStateV1& factory : state_->factories) {
            factory.dependencyScratch.clear();
            for (const std::size_t dependencyIndex : factory.dependencyIndices) {
                if (dependencyIndex >= state_->factories.size()) {
                    return rollbackWithoutPrimary(*state_, callbacks,
                                                  ProcessScopeErrorCodeV1::AdmittedTableInvalid);
                }
                const ResolvedProcessFactoryStateV1& dependency =
                    state_->factories[dependencyIndex];
                if (!dependency.active || !dependency.instance || !dependency.attribution) {
                    return rollbackWithoutPrimary(*state_, callbacks,
                                                  ProcessScopeErrorCodeV1::AdmittedTableInvalid);
                }
                appendDependencyView(factory, dependency.attribution->factory.view(),
                                     dependency.instance.value().view());
            }

            FactoryCreateContextV1 createContext = FactoryLifecycleContextAccessV1::create(
                processFactoryAttribution(factory), factory.dependencyScratch);
            FactoryCreateResultV1 createResult =
                callbacks[factory.descriptorIndex].create(createContext);
            if (!createResult.result().isSucceeded()) {
                return rollbackLifecycleFailure(*state_, callbacks, factory,
                                                ProcessScopeLifecycleStageV1::Create,
                                                ProcessScopeErrorCodeV1::FactoryCreateFailed,
                                                createResult.result().localCode());
            }

            factory.instance.emplace(std::move(createResult).takeInstance());
            FactoryActivateContextV1 activateContext =
                FactoryLifecycleContextAccessV1::activate(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 activateResult =
                callbacks[factory.descriptorIndex].activate(activateContext,
                                                            factory.instance->view());
            if (!activateResult.isSucceeded()) {
                return rollbackLifecycleFailure(
                    *state_, callbacks, factory, ProcessScopeLifecycleStageV1::Activate,
                    ProcessScopeErrorCodeV1::FactoryActivateFailed, activateResult.localCode());
            }
            factory.active = true;
        }

        state_->lifecycleState = ProcessScopeStateV1::Active;
        return {};
    }

} // namespace asharia::host_runtime

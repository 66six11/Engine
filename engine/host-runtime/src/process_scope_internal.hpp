#pragma once

#include <span>

#include "asharia/host_runtime/process_scope.hpp"

#include "admitted_static_factory_callback_table_access.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    class ProcessScopeOperationGuardV1 final {
    public:
        explicit ProcessScopeOperationGuardV1(bool& flag) noexcept : flag_(&flag) {
            flag = true;
        }

        ~ProcessScopeOperationGuardV1() {
            *flag_ = false;
        }

        ProcessScopeOperationGuardV1(const ProcessScopeOperationGuardV1&) = delete;
        ProcessScopeOperationGuardV1& operator=(const ProcessScopeOperationGuardV1&) = delete;
        ProcessScopeOperationGuardV1(ProcessScopeOperationGuardV1&&) = delete;
        ProcessScopeOperationGuardV1& operator=(ProcessScopeOperationGuardV1&&) = delete;

    private:
        bool* flag_{};
    };

    [[nodiscard]] ProcessScopeErrorCodeV1
    mapExecutionAccessError(AdmittedFactoryExecutionAccessErrorV1 error) noexcept;

    [[nodiscard]] ProcessScopeOperationErrorV1
    makeProcessScopeOperationError(ProcessScopeErrorCodeV1 code,
                                   ProcessScopeStateV1 state) noexcept;

    [[nodiscard]] FactoryAttributionViewV1
    processFactoryAttribution(const ResolvedProcessFactoryStateV1& factory) noexcept;

    [[nodiscard]] ProcessScopeLifecycleDiagnosticV1
    makeProcessScopeDiagnostic(const ResolvedProcessFactoryStateV1& factory,
                               ProcessScopeLifecycleStageV1 stage,
                               std::uint32_t providerLocalCode) noexcept;

    void appendProcessScopeCleanupDiagnostic(ProcessScopeExecutorStateV1& state,
                                             const ResolvedProcessFactoryStateV1& factory,
                                             ProcessScopeLifecycleStageV1 stage,
                                             std::uint32_t providerLocalCode) noexcept;

    void cleanupProcessScopeFactories(ProcessScopeExecutorStateV1& state,
                                      std::span<const StaticFactoryCallbacksV1> callbacks) noexcept;

} // namespace asharia::host_runtime

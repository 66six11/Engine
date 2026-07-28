#pragma once

#include <span>

#include "asharia/host_runtime/process_scope.hpp"

#include "admitted_static_factory_callback_table_access.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {

    class ProcessScopeOperationGuardV2 final {
    public:
        explicit ProcessScopeOperationGuardV2(bool& flag) noexcept : flag_(&flag) {
            flag = true;
        }

        ~ProcessScopeOperationGuardV2() {
            *flag_ = false;
        }

        ProcessScopeOperationGuardV2(const ProcessScopeOperationGuardV2&) = delete;
        ProcessScopeOperationGuardV2& operator=(const ProcessScopeOperationGuardV2&) = delete;
        ProcessScopeOperationGuardV2(ProcessScopeOperationGuardV2&&) = delete;
        ProcessScopeOperationGuardV2& operator=(ProcessScopeOperationGuardV2&&) = delete;

    private:
        bool* flag_{};
    };

    [[nodiscard]] ProcessScopeErrorCodeV2
    mapExecutionAccessError(AdmittedFactoryExecutionAccessErrorV2 error) noexcept;

    [[nodiscard]] ProcessScopeOperationErrorV2
    makeProcessScopeOperationError(ProcessScopeErrorCodeV2 code,
                                   ProcessScopeStateV2 state) noexcept;

    [[nodiscard]] FactoryAttributionViewV1
    processFactoryAttribution(const ResolvedProcessFactoryStateV2& factory) noexcept;

    [[nodiscard]] ProcessScopeLifecycleDiagnosticV2
    makeProcessScopeDiagnostic(const ResolvedProcessFactoryStateV2& factory,
                               ProcessScopeLifecycleStageV2 stage,
                               std::uint32_t providerLocalCode) noexcept;

    void appendProcessScopeCleanupDiagnostic(ProcessScopeExecutorStateV2& state,
                                             const ResolvedProcessFactoryStateV2& factory,
                                             ProcessScopeLifecycleStageV2 stage,
                                             std::uint32_t providerLocalCode) noexcept;

    void cleanupProcessScopeFactories(ProcessScopeExecutorStateV2& state,
                                      std::span<const StaticFactoryCallbacksV1> callbacks) noexcept;

} // namespace asharia::host_runtime

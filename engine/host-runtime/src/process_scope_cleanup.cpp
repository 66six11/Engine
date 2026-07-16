#include <cstddef>
#include <span>
#include <utility>

#include "factory_lifecycle_context_access.hpp"
#include "process_scope_internal.hpp"

namespace asharia::host_runtime {

    void
    cleanupProcessScopeFactories(ProcessScopeExecutorStateV1& state,
                                 std::span<const StaticFactoryCallbacksV1> callbacks) noexcept {
        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV1& factory = state.factories[index];
            if (!factory.active || !factory.instance) {
                continue;
            }
            FactoryQuiesceContextV1 context =
                FactoryLifecycleContextAccessV1::quiesce(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 result =
                callbacks[factory.descriptorIndex].quiesce(context, factory.instance->view());
            if (!result.isSucceeded()) {
                appendProcessScopeCleanupDiagnostic(
                    state, factory, ProcessScopeLifecycleStageV1::Quiesce, result.localCode());
            }
        }

        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV1& factory = state.factories[index];
            if (!factory.active || !factory.instance) {
                continue;
            }
            FactoryDeactivateContextV1 context =
                FactoryLifecycleContextAccessV1::deactivate(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 result =
                callbacks[factory.descriptorIndex].deactivate(context, factory.instance->view());
            if (!result.isSucceeded()) {
                appendProcessScopeCleanupDiagnostic(
                    state, factory, ProcessScopeLifecycleStageV1::Deactivate, result.localCode());
            }
            factory.active = false;
        }

        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV1& factory = state.factories[index];
            if (!factory.instance) {
                continue;
            }
            callbacks[factory.descriptorIndex].destroy(std::move(*factory.instance));
            factory.instance.reset();
        }
    }

} // namespace asharia::host_runtime

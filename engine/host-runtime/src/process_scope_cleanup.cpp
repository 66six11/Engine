#include <cstddef>
#include <span>
#include <utility>

#include "factory_lifecycle_context_access.hpp"
#include "process_scope_internal.hpp"

namespace asharia::host_runtime {

    void
    cleanupProcessScopeFactories(ProcessScopeExecutorStateV2& state,
                                 std::span<const StaticFactoryCallbacksV1> callbacks) noexcept {
        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV2& factory = state.factories[index];
            if (!factory.lifecycleActivated || !factory.instance) {
                continue;
            }
            FactoryQuiesceContextV1 context =
                FactoryLifecycleContextAccessV1::quiesce(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 result =
                callbacks[factory.descriptorIndex].quiesce(context, factory.instance->view());
            if (!result.isSucceeded()) {
                appendProcessScopeCleanupDiagnostic(
                    state, factory, ProcessScopeLifecycleStageV2::Quiesce, result.localCode());
            }
        }

        beginProcessContributionRegistryRevocationV1(state.contributionRegistry);
        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV2& factory = state.factories[index];
            factory.dependencyVisible = false;
            if (!factory.contributionLease) {
                continue;
            }
            factory.contributionLease->revoke();
            factory.contributionLease.reset();
        }

        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV2& factory = state.factories[index];
            if (!factory.lifecycleActivated || !factory.instance) {
                continue;
            }
            FactoryDeactivateContextV1 context =
                FactoryLifecycleContextAccessV1::deactivate(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 result =
                callbacks[factory.descriptorIndex].deactivate(context, factory.instance->view());
            if (!result.isSucceeded()) {
                appendProcessScopeCleanupDiagnostic(
                    state, factory, ProcessScopeLifecycleStageV2::Deactivate, result.localCode());
            }
            factory.lifecycleActivated = false;
        }

        for (std::size_t index = state.factories.size(); index-- > 0;) {
            ResolvedProcessFactoryStateV2& factory = state.factories[index];
            if (!factory.instance) {
                continue;
            }
            callbacks[factory.descriptorIndex].destroy(std::move(*factory.instance));
            factory.instance.reset();
        }

        finishProcessContributionRegistryRevocationV1(state.contributionRegistry);
    }

} // namespace asharia::host_runtime

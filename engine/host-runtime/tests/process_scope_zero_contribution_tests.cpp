#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::array kLifecyclePhases{
            SyntheticLifecyclePhaseV1::Create,  SyntheticLifecyclePhaseV1::Activate,
            SyntheticLifecyclePhaseV1::Quiesce, SyntheticLifecyclePhaseV1::Deactivate,
            SyntheticLifecyclePhaseV1::Destroy,
        };

        constexpr std::array kContributionSlots{
            SyntheticContributionSlotV1::Primary,
            SyntheticContributionSlotV1::ExtensionA,
            SyntheticContributionSlotV1::ExtensionB,
        };

        [[nodiscard]] bool selectedAccessorsCalledExactlyOnce() noexcept {
            return syntheticContributionAccessorObservation(SyntheticFactoryV1::Root,
                                                            SyntheticContributionSlotV1::Primary)
                           .invocationCount == 1 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                            SyntheticContributionSlotV1::ExtensionA)
                           .invocationCount == 1 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                            SyntheticContributionSlotV1::ExtensionB)
                           .invocationCount == 1 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Leaf,
                                                            SyntheticContributionSlotV1::ExtensionA)
                           .invocationCount == 1 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::ProjectOnly,
                                                            SyntheticContributionSlotV1::Primary)
                           .invocationCount == 0;
        }

        [[nodiscard]] bool emptyFactoryHasNoAccessor() noexcept {
            return std::ranges::all_of(kContributionSlots, [](SyntheticContributionSlotV1 slot) {
                const SyntheticContributionAccessorObservationV1 observation =
                    syntheticContributionAccessorObservation(SyntheticFactoryV1::Empty, slot);
                return observation.invocationCount == 0 && observation.nullReturnCount == 0 &&
                       !observation.nullReturnArmed &&
                       !injectSyntheticContributionNullOnce(SyntheticFactoryV1::Empty, slot);
            });
        }

        [[nodiscard]] bool selectedZeroContributionFactoryRunsWithoutPublishing() {
            resetSyntheticProviderFixture();
            auto admitted = makeAdmittedSyntheticProcessScope(
                ProcessPlanMutationV1::IncludeZeroContributionFactory);
            if (!admitted) {
                return false;
            }
            auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (!prepared || !prepared->start()) {
                return false;
            }

            const auto registry = prepared->contributions();
            bool registryCountsUnchanged = false;
            if (registry) {
                const auto primarySize = registry->size<SyntheticPrimaryServiceContractV1>();
                const auto extensionSize = registry->size<SyntheticExtensionContractV1>();
                registryCountsUnchanged =
                    primarySize && *primarySize == 1 && extensionSize && *extensionSize == 3;
            }

            const ProcessScopeStopResultV2 stopped = prepared->stop();
            const SyntheticFactoryObservationV1 empty =
                syntheticProviderObservation(SyntheticFactoryV1::Empty);
            const bool lifecycleRanExactlyOnce =
                std::ranges::all_of(kLifecyclePhases, [&empty](SyntheticLifecyclePhaseV1 phase) {
                    return empty.invocationCounts.at(static_cast<std::size_t>(phase)) == 1;
                });

            return registryCountsUnchanged && stopped && stopped->callbacksSucceeded() &&
                   lifecycleRanExactlyOnce && empty.expectedDependencyMask == 0 &&
                   empty.observedDependencyMask == 0 && empty.tokenIssueCount == 1 &&
                   empty.destroyCount == 1 && !empty.tokenOutstanding &&
                   selectedAccessorsCalledExactlyOnce() && emptyFactoryHasNoAccessor() &&
                   syntheticProjectOnlyInvocationCount() == 0 && syntheticProviderFixtureValid();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeZeroContributionTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "selected zero-contribution factory runs without publishing",
                .function = &selectedZeroContributionFactoryRunsWithoutPublishing,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

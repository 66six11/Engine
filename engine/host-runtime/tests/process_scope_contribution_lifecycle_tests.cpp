#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        template <typename Value>
        [[nodiscard]] bool
        hasLookupError(const std::expected<Value, ProcessContributionLookupErrorV1>& result,
                       ProcessContributionLookupErrorCodeV1 expected) noexcept {
            return !result && result.error().code == expected;
        }

        template <typename Value>
        [[nodiscard]] bool hasSupersededGenerationError(
            const std::expected<Value, ProcessContributionLookupErrorV1>& result) noexcept {
            return !result &&
                   (result.error().code ==
                        ProcessContributionLookupErrorCodeV1::WrongControlThread ||
                    result.error().code == ProcessContributionLookupErrorCodeV1::ProcessEpochStale);
        }
        [[nodiscard]] std::optional<ProcessScopeExecutorV2> preparedExecutor() {
            auto admitted = makeAdmittedSyntheticProcessScope();
            if (!admitted) {
                return std::nullopt;
            }
            auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (!prepared) {
                return std::nullopt;
            }
            return std::move(*prepared);
        }

        void stopAfterUnexpectedSetupFailure(ProcessScopeExecutorV2& executor) noexcept {
            if (!executor.stop()) {
                std::terminate();
            }
        }

        struct CleanupBorrowObservationV1 final {
            const ProcessContributionRegistryViewV1* registry{};
            const ProcessContributionHandleV1<SyntheticPrimaryServiceContractV1>* primaryHandle{};
            std::size_t quiesceCount{};
            std::size_t deactivateCount{};
            std::size_t destroyCount{};
            bool quiesceAvailable{true};
            bool deactivateRevoking{true};
            bool destroyRevoking{true};
        };

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        CleanupBorrowObservationV1 cleanupBorrowObservation;

        void inspectBorrowDuringCleanup([[maybe_unused]] SyntheticFactoryV1 factory,
                                        SyntheticLifecyclePhaseV1 phase) noexcept {
            const auto* const registry = cleanupBorrowObservation.registry;
            const auto* const handle = cleanupBorrowObservation.primaryHandle;
            if (registry == nullptr || handle == nullptr) {
                cleanupBorrowObservation.quiesceAvailable = false;
                cleanupBorrowObservation.deactivateRevoking = false;
                cleanupBorrowObservation.destroyRevoking = false;
                return;
            }

            const auto size = registry->size<SyntheticPrimaryServiceContractV1>();
            const auto borrowed = handle->tryBorrow();
            switch (phase) {
            case SyntheticLifecyclePhaseV1::Quiesce:
                ++cleanupBorrowObservation.quiesceCount;
                cleanupBorrowObservation.quiesceAvailable &=
                    size && *size == 1 && borrowed &&
                    borrowed->get().owner == SyntheticFactoryV1::Root;
                break;
            case SyntheticLifecyclePhaseV1::Deactivate:
                ++cleanupBorrowObservation.deactivateCount;
                cleanupBorrowObservation.deactivateRevoking &=
                    hasLookupError(size, ProcessContributionLookupErrorCodeV1::RegistryRevoking) &&
                    hasLookupError(borrowed,
                                   ProcessContributionLookupErrorCodeV1::RegistryRevoking);
                break;
            case SyntheticLifecyclePhaseV1::Destroy:
                ++cleanupBorrowObservation.destroyCount;
                cleanupBorrowObservation.destroyRevoking &=
                    hasLookupError(size, ProcessContributionLookupErrorCodeV1::RegistryRevoking) &&
                    hasLookupError(borrowed,
                                   ProcessContributionLookupErrorCodeV1::RegistryRevoking);
                break;
            case SyntheticLifecyclePhaseV1::Create:
            case SyntheticLifecyclePhaseV1::Activate:
            case SyntheticLifecyclePhaseV1::Count:
                break;
            }

            // The fixture hook is single-shot. Rearming here observes every
            // subsequent cleanup callback without changing lifecycle execution.
            armSyntheticProviderLifecycleHook(&inspectBorrowDuringCleanup);
        }

        [[nodiscard]] bool weakViewsAndHandlesDoNotExtendRegistryLifetime() {
            resetSyntheticProviderFixture();
            ProcessContributionRegistryViewV1 registry;
            std::optional<ProcessContributionHandleV1<SyntheticPrimaryServiceContractV1>>
                primaryHandle;
            bool stoppedCleanly = false;
            {
                auto executor = preparedExecutor();
                if (!executor || !executor->start()) {
                    return false;
                }
                const auto registryResult = executor->contributions();
                if (!registryResult) {
                    stopAfterUnexpectedSetupFailure(*executor);
                    return false;
                }
                registry = *registryResult;
                const auto handle = registry.single<SyntheticPrimaryServiceContractV1>();
                if (!handle || !handle->tryBorrow()) {
                    stopAfterUnexpectedSetupFailure(*executor);
                    return false;
                }
                primaryHandle.emplace(*handle);

                const ProcessScopeStopResultV2 stopped = executor->stop();
                stoppedCleanly =
                    stopped && stopped->callbacksSucceeded() &&
                    hasLookupError(registry.size<SyntheticPrimaryServiceContractV1>(),
                                   ProcessContributionLookupErrorCodeV1::RegistryRevoked) &&
                    hasLookupError(primaryHandle->tryBorrow(),
                                   ProcessContributionLookupErrorCodeV1::RegistryRevoked);
            }

            return stoppedCleanly &&
                   hasLookupError(registry.size<SyntheticPrimaryServiceContractV1>(),
                                  ProcessContributionLookupErrorCodeV1::RegistryExpired) &&
                   hasLookupError(primaryHandle->tryBorrow(),
                                  ProcessContributionLookupErrorCodeV1::RegistryExpired) &&
                   syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool cleanupGatePreservesQuiesceAndRevokesBeforeDeactivate() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor || !executor->start()) {
                return false;
            }
            const auto registryResult = executor->contributions();
            if (!registryResult) {
                stopAfterUnexpectedSetupFailure(*executor);
                return false;
            }
            ProcessContributionRegistryViewV1 registry = *registryResult;
            const auto primaryHandle = registry.single<SyntheticPrimaryServiceContractV1>();
            if (!primaryHandle) {
                stopAfterUnexpectedSetupFailure(*executor);
                return false;
            }

            cleanupBorrowObservation = {
                .registry = &registry,
                .primaryHandle = &*primaryHandle,
            };
            armSyntheticProviderLifecycleHook(&inspectBorrowDuringCleanup);
            const ProcessScopeStopResultV2 stopped = executor->stop();
            const bool revokedAfterStop =
                hasLookupError(registry.size<SyntheticPrimaryServiceContractV1>(),
                               ProcessContributionLookupErrorCodeV1::RegistryRevoked) &&
                hasLookupError(primaryHandle->tryBorrow(),
                               ProcessContributionLookupErrorCodeV1::RegistryRevoked);
            cleanupBorrowObservation.registry = nullptr;
            cleanupBorrowObservation.primaryHandle = nullptr;

            return stopped && stopped->callbacksSucceeded() && revokedAfterStop &&
                   cleanupBorrowObservation.quiesceCount == 3 &&
                   cleanupBorrowObservation.deactivateCount == 3 &&
                   cleanupBorrowObservation.destroyCount == 3 &&
                   cleanupBorrowObservation.quiesceAvailable &&
                   cleanupBorrowObservation.deactivateRevoking &&
                   cleanupBorrowObservation.destroyRevoking && syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool oldHandleFailsClosedAgainstLaterGeneration() {
            resetSyntheticProviderFixture();
            auto oldExecutor = preparedExecutor();
            if (!oldExecutor || !oldExecutor->start()) {
                return false;
            }
            const auto oldRegistryResult = oldExecutor->contributions();
            if (!oldRegistryResult) {
                stopAfterUnexpectedSetupFailure(*oldExecutor);
                return false;
            }
            const ProcessContributionRegistryViewV1& oldRegistry = *oldRegistryResult;
            const auto oldHandleResult = oldRegistry.single<SyntheticPrimaryServiceContractV1>();
            if (!oldHandleResult || !oldHandleResult->tryBorrow()) {
                stopAfterUnexpectedSetupFailure(*oldExecutor);
                return false;
            }
            const ProcessContributionHandleV1<SyntheticPrimaryServiceContractV1>& oldHandle =
                *oldHandleResult;
            const ProcessScopeStopResultV2 oldStopped = oldExecutor->stop();
            if (!oldStopped || !oldStopped->callbacksSucceeded() ||
                !hasLookupError(oldHandle.tryBorrow(),
                                ProcessContributionLookupErrorCodeV1::RegistryRevoked)) {
                return false;
            }

            rebindSyntheticCurrentProcessEpoch();
            if (!hasLookupError(oldRegistry.size<SyntheticPrimaryServiceContractV1>(),
                                ProcessContributionLookupErrorCodeV1::ProcessEpochStale) ||
                !hasLookupError(oldHandle.tryBorrow(),
                                ProcessContributionLookupErrorCodeV1::ProcessEpochStale)) {
                return false;
            }

            resetSyntheticProviderFixture();
            auto currentExecutor = preparedExecutor();
            if (!currentExecutor) {
                return false;
            }
            const bool oldGenerationStaleBeforeStart =
                hasSupersededGenerationError(
                    oldRegistry.size<SyntheticPrimaryServiceContractV1>()) &&
                hasSupersededGenerationError(oldHandle.tryBorrow());
            if (!currentExecutor->start()) {
                return false;
            }
            const auto currentRegistry = currentExecutor->contributions();
            if (!currentRegistry) {
                stopAfterUnexpectedSetupFailure(*currentExecutor);
                return false;
            }
            const auto currentHandle = currentRegistry->single<SyntheticPrimaryServiceContractV1>();
            if (!currentHandle) {
                stopAfterUnexpectedSetupFailure(*currentExecutor);
                return false;
            }
            const auto currentBorrow = currentHandle->tryBorrow();
            const bool generationsRemainIsolated =
                oldGenerationStaleBeforeStart && currentBorrow &&
                currentBorrow->get().owner == SyntheticFactoryV1::Root &&
                currentBorrow->get().contributionId == kSyntheticRootPrimaryContributionId &&
                hasSupersededGenerationError(oldHandle.tryBorrow());
            const ProcessScopeStopResultV2 currentStopped = currentExecutor->stop();

            return generationsRemainIsolated && currentStopped &&
                   currentStopped->callbacksSucceeded() &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Root,
                                                            SyntheticContributionSlotV1::Primary)
                           .invocationCount == 1 &&
                   syntheticProviderFixtureValid();
        }
        [[nodiscard]] bool publicationDiagnosticMatches(
            const ProcessScopeContributionPublicationDiagnosticV2& diagnostic,
            SyntheticFactoryV1 factory, std::string_view contributionId,
            std::string_view contributionKind,
            ProcessScopeContributionPublicationStageV2 stage) noexcept {
            const ExactFactoryReferenceViewV1 reference = diagnostic.factory();
            return diagnostic.stage() == stage &&
                   diagnostic.engineGenerationId() == kSyntheticEngineGenerationId &&
                   reference.packageId == kSyntheticPackageId &&
                   reference.packageVersion == kSyntheticPackageVersion &&
                   reference.moduleId == kSyntheticModuleId &&
                   reference.factoryId == syntheticFactoryId(factory) &&
                   diagnostic.contributionId() == contributionId &&
                   diagnostic.contributionKind() == contributionKind;
        }

        constexpr std::array kPublicationFailureRollbackTrace{
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Root,
                .phase = SyntheticLifecyclePhaseV1::Create,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Root,
                .phase = SyntheticLifecyclePhaseV1::Activate,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Middle,
                .phase = SyntheticLifecyclePhaseV1::Create,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Middle,
                .phase = SyntheticLifecyclePhaseV1::Activate,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Middle,
                .phase = SyntheticLifecyclePhaseV1::Quiesce,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Root,
                .phase = SyntheticLifecyclePhaseV1::Quiesce,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Middle,
                .phase = SyntheticLifecyclePhaseV1::Deactivate,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Root,
                .phase = SyntheticLifecyclePhaseV1::Deactivate,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Middle,
                .phase = SyntheticLifecyclePhaseV1::Destroy,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Root,
                .phase = SyntheticLifecyclePhaseV1::Destroy,
            },
        };

        [[nodiscard]] bool nullAccessorRollsBackAtomicPublication() {
            resetSyntheticProviderFixture();
            if (!injectSyntheticContributionNullOnce(SyntheticFactoryV1::Middle,
                                                     SyntheticContributionSlotV1::ExtensionB)) {
                return false;
            }
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }

            const ProcessScopeStartResultV2 started = executor->start();
            if (started ||
                started.error().operation.code !=
                    ProcessScopeErrorCodeV2::ContributionAccessorReturnedNull ||
                started.error().operation.state != ProcessScopeStateV2::StartFailed ||
                started.error().primary || !started.error().publication ||
                !started.error().cleanupDiagnostics.empty()) {
                return false;
            }

            const auto unavailable = executor->contributions();
            const SyntheticContributionAccessorObservationV1 rootPrimary =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::Root,
                                                         SyntheticContributionSlotV1::Primary);
            const SyntheticContributionAccessorObservationV1 middleA =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                         SyntheticContributionSlotV1::ExtensionA);
            const SyntheticContributionAccessorObservationV1 middleB =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                         SyntheticContributionSlotV1::ExtensionB);
            const SyntheticContributionAccessorObservationV1 leafA =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::Leaf,
                                                         SyntheticContributionSlotV1::ExtensionA);
            const SyntheticContributionAccessorObservationV1 projectPrimary =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::ProjectOnly,
                                                         SyntheticContributionSlotV1::Primary);

            return publicationDiagnosticMatches(
                       *started.error().publication, SyntheticFactoryV1::Middle,
                       kSyntheticMiddleExtensionBContributionId, SyntheticExtensionContractV1::kind,
                       ProcessScopeContributionPublicationStageV2::PayloadAccess) &&
                   executor->state() == ProcessScopeStateV2::StartFailed &&
                   hasLookupError(unavailable,
                                  ProcessContributionLookupErrorCodeV1::RegistryNotActive) &&
                   std::ranges::equal(syntheticProviderTrace(), kPublicationFailureRollbackTrace) &&
                   rootPrimary.invocationCount == 1 && rootPrimary.nullReturnCount == 0 &&
                   middleA.invocationCount == 1 && middleA.nullReturnCount == 0 &&
                   middleB.invocationCount == 1 && middleB.nullReturnCount == 1 &&
                   !middleB.nullReturnArmed && leafA.invocationCount == 0 &&
                   projectPrimary.invocationCount == 0 &&
                   syntheticProjectOnlyInvocationCount() == 0 && syntheticProviderFixtureValid();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeContributionLifecycleTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "weak contribution handles do not extend registry lifetime",
                .function = &weakViewsAndHandlesDoNotExtendRegistryLifetime,
            },
            NamedProcessScopeTestV1{
                .name = "contributions survive quiesce and revoke before deactivate",
                .function = &cleanupGatePreservesQuiesceAndRevokesBeforeDeactivate,
            },
            NamedProcessScopeTestV1{
                .name = "old contribution handle fails closed against later generation",
                .function = &oldHandleFailsClosedAgainstLaterGeneration,
            },
            NamedProcessScopeTestV1{
                .name = "null contribution accessor rolls back atomically",
                .function = &nullAccessorRollsBackAtomicPublication,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

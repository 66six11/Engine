#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::array<SyntheticFactoryV1, 3> kProcessFactories{
            SyntheticFactoryV1::Root,
            SyntheticFactoryV1::Middle,
            SyntheticFactoryV1::Leaf,
        };

        constexpr std::array kExpectedStartupTrace{
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
                .factory = SyntheticFactoryV1::Leaf,
                .phase = SyntheticLifecyclePhaseV1::Create,
            },
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Leaf,
                .phase = SyntheticLifecyclePhaseV1::Activate,
            },
        };

        struct ReentrantStartObservationV1 final {
            ProcessScopeExecutorV2* executor{};
            std::size_t invocationCount{};
            std::size_t traceSizeBefore{};
            std::size_t traceSizeAfter{};
            SyntheticFactoryV1 factory{SyntheticFactoryV1::Middle};
            SyntheticLifecyclePhaseV1 phase{SyntheticLifecyclePhaseV1::Create};
            ProcessScopeErrorCodeV2 errorCode{ProcessScopeErrorCodeV2::ExecutorMovedFrom};
            ProcessScopeStateV2 errorState{ProcessScopeStateV2::MovedFrom};
            bool resultSucceeded{};
        };

        ReentrantStartObservationV1
            reentrantStartObservation; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        void reenterStart(SyntheticFactoryV1 factory, SyntheticLifecyclePhaseV1 phase) noexcept {
            ++reentrantStartObservation.invocationCount;
            reentrantStartObservation.factory = factory;
            reentrantStartObservation.phase = phase;
            reentrantStartObservation.traceSizeBefore = syntheticProviderTrace().size();
            if (reentrantStartObservation.executor != nullptr) {
                const ProcessScopeStartResultV2 result =
                    reentrantStartObservation.executor->start();
                reentrantStartObservation.resultSucceeded = result.has_value();
                if (!result) {
                    reentrantStartObservation.errorCode = result.error().operation.code;
                    reentrantStartObservation.errorState = result.error().operation.state;
                }
            }
            reentrantStartObservation.traceSizeAfter = syntheticProviderTrace().size();
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

        [[nodiscard]] bool exactFactory(ExactFactoryReferenceViewV1 reference,
                                        SyntheticFactoryV1 factory) noexcept {
            return reference.packageId == kSyntheticPackageId &&
                   reference.packageVersion == kSyntheticPackageVersion &&
                   reference.moduleId == kSyntheticModuleId &&
                   reference.factoryId == syntheticFactoryId(factory);
        }

        struct StartupFailureCaseV1 final {
            SyntheticFactoryV1 failedFactory{SyntheticFactoryV1::Root};
            SyntheticLifecyclePhaseV1 failedPhase{SyntheticLifecyclePhaseV1::Create};
            std::size_t failurePosition{};
            std::uint32_t localCode{};
        };

        [[nodiscard]] constexpr std::size_t
        processFactoryPosition(SyntheticFactoryV1 factory) noexcept {
            switch (factory) {
            case SyntheticFactoryV1::Root:
                return 0;
            case SyntheticFactoryV1::Middle:
                return 1;
            case SyntheticFactoryV1::Leaf:
                return 2;
            case SyntheticFactoryV1::ProjectOnly:
            case SyntheticFactoryV1::Empty:
            case SyntheticFactoryV1::Count:
                break;
            }
            return kProcessFactories.size();
        }

        [[nodiscard]] constexpr ProcessScopeLifecycleStageV2
        lifecycleStageFor(SyntheticLifecyclePhaseV1 phase) noexcept {
            return phase == SyntheticLifecyclePhaseV1::Create
                       ? ProcessScopeLifecycleStageV2::Create
                       : ProcessScopeLifecycleStageV2::Activate;
        }

        [[nodiscard]] bool
        startupFailureDiagnosticMatches(const ProcessScopeStartFailureV2& failure,
                                        const StartupFailureCaseV1& failureCase) noexcept {
            if (!failure.primary.has_value()) {
                return false;
            }
            const ProcessScopeLifecycleDiagnosticV2& primary = *failure.primary;
            const ProcessScopeErrorCodeV2 expectedOperationCode =
                failureCase.failedPhase == SyntheticLifecyclePhaseV1::Create
                    ? ProcessScopeErrorCodeV2::FactoryCreateFailed
                    : ProcessScopeErrorCodeV2::FactoryActivateFailed;
            return failure.operation.code == expectedOperationCode &&
                   failure.operation.state == ProcessScopeStateV2::StartFailed &&
                   primary.providerLocalCode() == failureCase.localCode &&
                   exactFactory(primary.factory(), failureCase.failedFactory) &&
                   primary.engineGenerationId() == kSyntheticEngineGenerationId &&
                   primary.stage() == lifecycleStageFor(failureCase.failedPhase) &&
                   failure.cleanupDiagnostics.empty();
        }

        [[nodiscard]] bool factoryCountsMatchFailureCase(const StartupFailureCaseV1& failureCase,
                                                         SyntheticFactoryV1 factory) noexcept {
            const std::size_t factoryPosition = processFactoryPosition(factory);
            const bool beforeFailure = factoryPosition < failureCase.failurePosition;
            const bool isFailure = factory == failureCase.failedFactory;
            const bool createExpected = beforeFailure || isFailure;
            const bool activateExpected =
                beforeFailure ||
                (isFailure && failureCase.failedPhase == SyntheticLifecyclePhaseV1::Activate);
            const SyntheticFactoryObservationV1 observation = syntheticProviderObservation(factory);
            return observation.invocationCounts.at(
                       static_cast<std::size_t>(SyntheticLifecyclePhaseV1::Create)) ==
                       static_cast<std::size_t>(createExpected) &&
                   observation.invocationCounts.at(
                       static_cast<std::size_t>(SyntheticLifecyclePhaseV1::Activate)) ==
                       static_cast<std::size_t>(activateExpected) &&
                   observation.invocationCounts.at(
                       static_cast<std::size_t>(SyntheticLifecyclePhaseV1::Quiesce)) ==
                       static_cast<std::size_t>(beforeFailure) &&
                   observation.invocationCounts.at(
                       static_cast<std::size_t>(SyntheticLifecyclePhaseV1::Deactivate)) ==
                       static_cast<std::size_t>(beforeFailure) &&
                   observation.destroyCount == static_cast<std::size_t>(activateExpected) &&
                   !observation.tokenOutstanding;
        }

        [[nodiscard]] bool
        startupFailureCaseRollsBackExactlyOnce(const StartupFailureCaseV1& failureCase) {
            resetSyntheticProviderFixture();
            if (!injectSyntheticProviderFailure(failureCase.failedFactory, failureCase.failedPhase,
                                                failureCase.localCode)) {
                return false;
            }
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }
            const ProcessScopeStartResultV2 started = executor->start();
            if (started || executor->state() != ProcessScopeStateV2::StartFailed ||
                !startupFailureDiagnosticMatches(started.error(), failureCase) ||
                !std::ranges::all_of(kProcessFactories, [&failureCase](SyntheticFactoryV1 factory) {
                    return factoryCountsMatchFailureCase(failureCase, factory);
                })) {
                return false;
            }

            const std::size_t traceSize = syntheticProviderTrace().size();
            const ProcessScopeStartResultV2 replay = executor->start();
            return !replay &&
                   replay.error().operation.code ==
                       ProcessScopeErrorCodeV2::StartRequiresPrepared &&
                   syntheticProviderTrace().size() == traceSize &&
                   syntheticProviderFixtureValid() && syntheticProjectOnlyInvocationCount() == 0;
        }

        [[nodiscard]] bool blueprintOrderDrivesStartup() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor || !executor->start()) {
                return false;
            }
            const auto trace = syntheticProviderTrace();
            if (!std::ranges::equal(trace, kExpectedStartupTrace) ||
                syntheticProjectOnlyInvocationCount() != 0) {
                return false;
            }
            for (const SyntheticFactoryV1 factory : kProcessFactories) {
                const SyntheticFactoryObservationV1 observation =
                    syntheticProviderObservation(factory);
                if (observation.observedDependencyMask != observation.expectedDependencyMask ||
                    observation.tokenIssueCount != 1 || observation.destroyCount != 0 ||
                    !observation.tokenOutstanding) {
                    return false;
                }
            }
            return syntheticProviderFixtureValid() && executor->stop().has_value();
        }

        [[nodiscard]] bool reentrantStartDuringCreateIsRejected() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }
            reentrantStartObservation = {
                .executor = &*executor,
            };
            armSyntheticProviderLifecycleHook(&reenterStart);

            const ProcessScopeStartResultV2 started = executor->start();
            if (!started) {
                return false;
            }
            const bool guarded =
                reentrantStartObservation.invocationCount == 1 &&
                reentrantStartObservation.factory == SyntheticFactoryV1::Root &&
                reentrantStartObservation.phase == SyntheticLifecyclePhaseV1::Create &&
                !reentrantStartObservation.resultSucceeded &&
                reentrantStartObservation.errorCode ==
                    ProcessScopeErrorCodeV2::OperationInProgress &&
                reentrantStartObservation.errorState == ProcessScopeStateV2::Prepared &&
                reentrantStartObservation.traceSizeBefore == 1 &&
                reentrantStartObservation.traceSizeAfter == 1 &&
                std::ranges::equal(syntheticProviderTrace(), kExpectedStartupTrace) &&
                executor->state() == ProcessScopeStateV2::Active &&
                syntheticProjectOnlyInvocationCount() == 0 && syntheticProviderFixtureValid();
            const ProcessScopeStopResultV2 stopped = executor->stop();
            return guarded && stopped && stopped->callbacksSucceeded();
        }

        [[nodiscard]] bool startupFailureMatrixRollsBackExactlyOnce() {
            constexpr std::array failureCases{
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Root,
                    .failedPhase = SyntheticLifecyclePhaseV1::Create,
                    .failurePosition = 0,
                    .localCode = 100,
                },
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Root,
                    .failedPhase = SyntheticLifecyclePhaseV1::Activate,
                    .failurePosition = 0,
                    .localCode = 101,
                },
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Middle,
                    .failedPhase = SyntheticLifecyclePhaseV1::Create,
                    .failurePosition = 1,
                    .localCode = 102,
                },
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Middle,
                    .failedPhase = SyntheticLifecyclePhaseV1::Activate,
                    .failurePosition = 1,
                    .localCode = 103,
                },
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Leaf,
                    .failedPhase = SyntheticLifecyclePhaseV1::Create,
                    .failurePosition = 2,
                    .localCode = 104,
                },
                StartupFailureCaseV1{
                    .failedFactory = SyntheticFactoryV1::Leaf,
                    .failedPhase = SyntheticLifecyclePhaseV1::Activate,
                    .failurePosition = 2,
                    .localCode = 105,
                },
            };
            return std::ranges::all_of(failureCases, &startupFailureCaseRollsBackExactlyOnce);
        }

        [[nodiscard]] bool staleEpochAfterPreparationIsTerminal() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }
            rebindSyntheticCurrentProcessEpoch();
            const auto started = executor->start();
            if (started ||
                started.error().operation.code != ProcessScopeErrorCodeV2::ProcessEpochStale ||
                executor->state() != ProcessScopeStateV2::StartFailed ||
                !syntheticProviderTrace().empty()) {
                return false;
            }
            const auto replay = executor->start();
            return !replay &&
                   replay.error().operation.code ==
                       ProcessScopeErrorCodeV2::StartRequiresPrepared &&
                   syntheticProviderTrace().empty();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeStartupTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "Blueprint order drives startup",
                .function = &blueprintOrderDrivesStartup,
            },
            NamedProcessScopeTestV1{
                .name = "reentrant start during create is rejected",
                .function = &reentrantStartDuringCreateIsRejected,
            },
            NamedProcessScopeTestV1{
                .name = "startup failure matrix rolls back exactly once",
                .function = &startupFailureMatrixRollsBackExactlyOnce,
            },
            NamedProcessScopeTestV1{
                .name = "stale epoch after preparation is terminal",
                .function = &staleEpochAfterPreparationIsTerminal,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

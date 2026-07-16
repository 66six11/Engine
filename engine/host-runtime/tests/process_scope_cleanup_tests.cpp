#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::array kExpectedStopTrace{
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
            SyntheticLifecycleEventV1{
                .factory = SyntheticFactoryV1::Leaf,
                .phase = SyntheticLifecyclePhaseV1::Quiesce,
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
                .factory = SyntheticFactoryV1::Leaf,
                .phase = SyntheticLifecyclePhaseV1::Deactivate,
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
                .factory = SyntheticFactoryV1::Leaf,
                .phase = SyntheticLifecyclePhaseV1::Destroy,
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

        struct ReentrantStopObservationV1 final {
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

        ReentrantStopObservationV1
            reentrantStopObservation; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        void reenterStop(SyntheticFactoryV1 factory, SyntheticLifecyclePhaseV1 phase) noexcept {
            ++reentrantStopObservation.invocationCount;
            reentrantStopObservation.factory = factory;
            reentrantStopObservation.phase = phase;
            reentrantStopObservation.traceSizeBefore = syntheticProviderTrace().size();
            if (reentrantStopObservation.executor != nullptr) {
                const ProcessScopeStopResultV2 result = reentrantStopObservation.executor->stop();
                reentrantStopObservation.resultSucceeded = result.has_value();
                if (!result) {
                    reentrantStopObservation.errorCode = result.error().code;
                    reentrantStopObservation.errorState = result.error().state;
                }
            }
            reentrantStopObservation.traceSizeAfter = syntheticProviderTrace().size();
        }

        [[nodiscard]] std::optional<ProcessScopeExecutorV2> activeExecutor() {
            auto admitted = makeAdmittedSyntheticProcessScope();
            if (!admitted) {
                return std::nullopt;
            }
            auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (!prepared || !prepared->start()) {
                return std::nullopt;
            }
            return std::move(*prepared);
        }

        [[nodiscard]] bool diagnosticMatches(const ProcessScopeLifecycleDiagnosticV2& diagnostic,
                                             SyntheticFactoryV1 factory,
                                             ProcessScopeLifecycleStageV2 stage,
                                             std::uint32_t localCode) noexcept {
            const ExactFactoryReferenceViewV1 reference = diagnostic.factory();
            return diagnostic.stage() == stage && diagnostic.providerLocalCode() == localCode &&
                   diagnostic.engineGenerationId() == kSyntheticEngineGenerationId &&
                   reference.packageId == kSyntheticPackageId &&
                   reference.packageVersion == kSyntheticPackageVersion &&
                   reference.moduleId == kSyntheticModuleId &&
                   reference.factoryId == syntheticFactoryId(factory);
        }

        [[nodiscard]] bool normalStopUsesThreeReversePasses() {
            resetSyntheticProviderFixture();
            auto executor = activeExecutor();
            if (!executor) {
                return false;
            }
            const auto stopped = executor->stop();
            if (!stopped || !stopped->callbacksSucceeded()) {
                return false;
            }
            if (!std::ranges::equal(syntheticProviderTrace(), kExpectedStopTrace) ||
                executor->state() != ProcessScopeStateV2::Stopped ||
                syntheticProjectOnlyInvocationCount() != 0) {
                return false;
            }
            constexpr std::array processFactories{
                SyntheticFactoryV1::Root,
                SyntheticFactoryV1::Middle,
                SyntheticFactoryV1::Leaf,
            };
            return std::ranges::all_of(processFactories,
                                       [](SyntheticFactoryV1 factory) {
                                           const SyntheticFactoryObservationV1 observation =
                                               syntheticProviderObservation(factory);
                                           return observation.tokenIssueCount == 1 &&
                                                  observation.destroyCount == 1 &&
                                                  !observation.tokenOutstanding;
                                       }) &&
                   syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool reentrantStopDuringQuiesceIsRejected() {
            resetSyntheticProviderFixture();
            auto executor = activeExecutor();
            if (!executor) {
                return false;
            }
            reentrantStopObservation = {
                .executor = &*executor,
            };
            armSyntheticProviderLifecycleHook(&reenterStop);

            const ProcessScopeStopResultV2 stopped = executor->stop();
            return stopped && stopped->callbacksSucceeded() &&
                   reentrantStopObservation.invocationCount == 1 &&
                   reentrantStopObservation.factory == SyntheticFactoryV1::Leaf &&
                   reentrantStopObservation.phase == SyntheticLifecyclePhaseV1::Quiesce &&
                   !reentrantStopObservation.resultSucceeded &&
                   reentrantStopObservation.errorCode ==
                       ProcessScopeErrorCodeV2::OperationInProgress &&
                   reentrantStopObservation.errorState == ProcessScopeStateV2::Active &&
                   reentrantStopObservation.traceSizeBefore == 7 &&
                   reentrantStopObservation.traceSizeAfter == 7 &&
                   std::ranges::equal(syntheticProviderTrace(), kExpectedStopTrace) &&
                   executor->state() == ProcessScopeStateV2::Stopped &&
                   syntheticProjectOnlyInvocationCount() == 0 && syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool cleanupFailuresAreAggregatedWithoutTruncation() {
            resetSyntheticProviderFixture();
            if (!injectSyntheticProviderFailure(SyntheticFactoryV1::Leaf,
                                                SyntheticLifecyclePhaseV1::Quiesce, 41) ||
                !injectSyntheticProviderFailure(SyntheticFactoryV1::Root,
                                                SyntheticLifecyclePhaseV1::Quiesce, 42) ||
                !injectSyntheticProviderFailure(SyntheticFactoryV1::Middle,
                                                SyntheticLifecyclePhaseV1::Deactivate, 43)) {
                return false;
            }
            ProcessScopeStopReportV2 report;
            {
                auto executor = activeExecutor();
                if (!executor) {
                    return false;
                }
                auto stopped = executor->stop();
                if (!stopped || stopped->callbacksSucceeded() ||
                    executor->state() != ProcessScopeStateV2::Stopped) {
                    return false;
                }
                report = std::move(*stopped);
            }

            return report.cleanupDiagnostics.size() == 3 &&
                   diagnosticMatches(report.cleanupDiagnostics.at(0), SyntheticFactoryV1::Leaf,
                                     ProcessScopeLifecycleStageV2::Quiesce, 41) &&
                   diagnosticMatches(report.cleanupDiagnostics.at(1), SyntheticFactoryV1::Root,
                                     ProcessScopeLifecycleStageV2::Quiesce, 42) &&
                   diagnosticMatches(report.cleanupDiagnostics.at(2), SyntheticFactoryV1::Middle,
                                     ProcessScopeLifecycleStageV2::Deactivate, 43) &&
                   syntheticProviderInvocationCount(SyntheticFactoryV1::Root,
                                                    SyntheticLifecyclePhaseV1::Destroy) == 1 &&
                   syntheticProviderInvocationCount(SyntheticFactoryV1::Middle,
                                                    SyntheticLifecyclePhaseV1::Destroy) == 1 &&
                   syntheticProviderInvocationCount(SyntheticFactoryV1::Leaf,
                                                    SyntheticLifecyclePhaseV1::Destroy) == 1 &&
                   syntheticProviderFixtureValid() && syntheticProjectOnlyInvocationCount() == 0;
        }

        [[nodiscard]] bool rollbackCleanupFailuresPreservePrimaryFailure() {
            resetSyntheticProviderFixture();
            if (!injectSyntheticProviderFailure(SyntheticFactoryV1::Middle,
                                                SyntheticLifecyclePhaseV1::Activate, 31) ||
                !injectSyntheticProviderFailure(SyntheticFactoryV1::Root,
                                                SyntheticLifecyclePhaseV1::Quiesce, 41) ||
                !injectSyntheticProviderFailure(SyntheticFactoryV1::Root,
                                                SyntheticLifecyclePhaseV1::Deactivate, 42)) {
                return false;
            }
            auto admitted = makeAdmittedSyntheticProcessScope();
            if (!admitted) {
                return false;
            }
            auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (!prepared) {
                return false;
            }
            const ProcessScopeStartResultV2 started = prepared->start();
            if (started) {
                return false;
            }
            const ProcessScopeStartFailureV2& failure = started.error();
            if (!failure.primary.has_value()) {
                return false;
            }
            const ProcessScopeLifecycleDiagnosticV2& primary = *failure.primary;
            if (!diagnosticMatches(primary, SyntheticFactoryV1::Middle,
                                   ProcessScopeLifecycleStageV2::Activate, 31) ||
                failure.cleanupDiagnostics.size() != 2) {
                return false;
            }
            return diagnosticMatches(failure.cleanupDiagnostics.at(0), SyntheticFactoryV1::Root,
                                     ProcessScopeLifecycleStageV2::Quiesce, 41) &&
                   diagnosticMatches(failure.cleanupDiagnostics.at(1), SyntheticFactoryV1::Root,
                                     ProcessScopeLifecycleStageV2::Deactivate, 42) &&
                   syntheticProviderInvocationCount(SyntheticFactoryV1::Middle,
                                                    SyntheticLifecyclePhaseV1::Destroy) == 1 &&
                   syntheticProviderInvocationCount(SyntheticFactoryV1::Root,
                                                    SyntheticLifecyclePhaseV1::Destroy) == 1 &&
                   syntheticProviderFixtureValid();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeCleanupTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "normal stop uses three reverse passes",
                .function = &normalStopUsesThreeReversePasses,
            },
            NamedProcessScopeTestV1{
                .name = "reentrant stop during quiesce is rejected",
                .function = &reentrantStopDuringQuiesceIsRejected,
            },
            NamedProcessScopeTestV1{
                .name = "cleanup failures aggregate without truncation",
                .function = &cleanupFailuresAreAggregatedWithoutTruncation,
            },
            NamedProcessScopeTestV1{
                .name = "rollback cleanup preserves primary failure",
                .function = &rollbackCleanupFailuresPreservePrimaryFailure,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

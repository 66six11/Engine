#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        struct AbsentContributionContractV1 final {
            static constexpr std::string_view kind{"com.asharia.test.process-scope.absent"};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Multiple};
        };

        struct WrongTypeExtensionContractV1 final {
            static constexpr std::string_view kind{SyntheticExtensionContractV1::kind};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Multiple};
        };

        struct WrongCardinalityExtensionContractV1 final {
            static constexpr std::string_view kind{SyntheticExtensionContractV1::kind};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Single};
        };

        template <typename Contract>
        concept SupportsSingleLookupV1 =
            requires(const ProcessContributionRegistryViewV1& registry) {
                registry.template single<Contract>();
            };

        template <typename Contract>
        concept SupportsOrdinalLookupV1 =
            requires(const ProcessContributionRegistryViewV1& registry) {
                registry.template at<Contract>(0);
            };

        class ActiveExecutorStopGuardV1 final {
        public:
            explicit ActiveExecutorStopGuardV1(ProcessScopeExecutorV2& executor) noexcept
                : executor_(&executor) {}

            ~ActiveExecutorStopGuardV1() noexcept {
                if (executor_ != nullptr && executor_->state() == ProcessScopeStateV2::Active) {
                    if (!executor_->stop()) {
                        std::terminate();
                    }
                }
            }

            ActiveExecutorStopGuardV1(const ActiveExecutorStopGuardV1&) = delete;
            ActiveExecutorStopGuardV1& operator=(const ActiveExecutorStopGuardV1&) = delete;
            ActiveExecutorStopGuardV1(ActiveExecutorStopGuardV1&&) = delete;
            ActiveExecutorStopGuardV1& operator=(ActiveExecutorStopGuardV1&&) = delete;

        private:
            ProcessScopeExecutorV2* executor_;
        };

        [[nodiscard]] std::optional<ProcessScopeExecutorV2>
        preparedExecutor(ProcessPlanMutationV1 mutation = ProcessPlanMutationV1::None) {
            auto admitted = makeAdmittedSyntheticProcessScope(mutation);
            if (!admitted) {
                return std::nullopt;
            }
            auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (!prepared) {
                return std::nullopt;
            }
            return std::move(*prepared);
        }

        template <typename Value>
        [[nodiscard]] bool
        hasLookupError(const std::expected<Value, ProcessContributionLookupErrorV1>& result,
                       ProcessContributionLookupErrorCodeV1 expected) noexcept {
            return !result && result.error().code == expected;
        }

        [[nodiscard]] bool allSyntheticContributionAccessorsUnused() noexcept {
            return syntheticContributionAccessorObservation(SyntheticFactoryV1::Root,
                                                            SyntheticContributionSlotV1::Primary)
                           .invocationCount == 0 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                            SyntheticContributionSlotV1::ExtensionA)
                           .invocationCount == 0 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Middle,
                                                            SyntheticContributionSlotV1::ExtensionB)
                           .invocationCount == 0 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::Leaf,
                                                            SyntheticContributionSlotV1::ExtensionA)
                           .invocationCount == 0 &&
                   syntheticContributionAccessorObservation(SyntheticFactoryV1::ProjectOnly,
                                                            SyntheticContributionSlotV1::Primary)
                           .invocationCount == 0;
        }

        [[nodiscard]] bool publicLookupCardinalityIsConstrainedAtCompileTime() {
            static_assert(ProcessContributionContractV1<SyntheticPrimaryServiceContractV1>);
            static_assert(ProcessContributionContractV1<SyntheticExtensionContractV1>);
            static_assert(SupportsSingleLookupV1<SyntheticPrimaryServiceContractV1>);
            static_assert(!SupportsOrdinalLookupV1<SyntheticPrimaryServiceContractV1>);
            static_assert(!SupportsSingleLookupV1<SyntheticExtensionContractV1>);
            static_assert(SupportsOrdinalLookupV1<SyntheticExtensionContractV1>);
            static_assert(!std::is_default_constructible_v<
                          ProcessContributionHandleV1<SyntheticPrimaryServiceContractV1>>);

            using SingleLookupResult =
                decltype(std::declval<const ProcessContributionRegistryViewV1&>()
                             .single<SyntheticPrimaryServiceContractV1>());
            using ExpectedSingleLookupResult =
                std::expected<ProcessContributionHandleV1<SyntheticPrimaryServiceContractV1>,
                              ProcessContributionLookupErrorV1>;
            static_assert(std::same_as<SingleLookupResult, ExpectedSingleLookupResult>);

            using BorrowResult = decltype(std::declval<const ProcessContributionHandleV1<
                                              SyntheticPrimaryServiceContractV1>&>()
                                              .tryBorrow());
            using ExpectedBorrowResult =
                std::expected<std::reference_wrapper<SyntheticPrimaryServiceContractV1>,
                              ProcessContributionLookupErrorV1>;
            static_assert(std::same_as<BorrowResult, ExpectedBorrowResult>);
            return true;
        }

        [[nodiscard]] bool contributionsAreUnavailableBeforeStart() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }

            const auto contributions = executor->contributions();
            return hasLookupError(contributions,
                                  ProcessContributionLookupErrorCodeV1::RegistryNotActive) &&
                   executor->state() == ProcessScopeStateV2::Prepared &&
                   syntheticProviderTrace().empty() && allSyntheticContributionAccessorsUnused() &&
                   syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool activeRegistryPublishesStableTypedLookups() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor || !executor->start()) {
                return false;
            }
            ActiveExecutorStopGuardV1 stopGuard{*executor};

            const auto registryResult = executor->contributions();
            if (!registryResult) {
                return false;
            }
            const ProcessContributionRegistryViewV1& registry = *registryResult;
            const auto primarySize = registry.size<SyntheticPrimaryServiceContractV1>();
            const auto primaryHandle = registry.single<SyntheticPrimaryServiceContractV1>();
            if (!primarySize || *primarySize != 1 || !primaryHandle) {
                return false;
            }
            const auto primary = primaryHandle->tryBorrow();
            if (!primary || primary->get().owner != SyntheticFactoryV1::Root ||
                primary->get().contributionId != kSyntheticRootPrimaryContributionId) {
                return false;
            }

            const auto extensionSize = registry.size<SyntheticExtensionContractV1>();
            if (!extensionSize || *extensionSize != 3) {
                return false;
            }
            struct ExpectedExtensionV1 final {
                SyntheticFactoryV1 owner;
                std::string_view contributionId;
            };
            constexpr std::array expected{
                ExpectedExtensionV1{
                    .owner = SyntheticFactoryV1::Middle,
                    .contributionId = kSyntheticMiddleExtensionAContributionId,
                },
                ExpectedExtensionV1{
                    .owner = SyntheticFactoryV1::Middle,
                    .contributionId = kSyntheticMiddleExtensionBContributionId,
                },
                ExpectedExtensionV1{
                    .owner = SyntheticFactoryV1::Leaf,
                    .contributionId = kSyntheticLeafExtensionContributionId,
                },
            };

            bool ordinalOrderMatches = true;
            for (std::size_t ordinal = 0; ordinal < expected.size(); ++ordinal) {
                const ExpectedExtensionV1& expectedContribution = expected.at(ordinal);
                const auto handle = registry.at<SyntheticExtensionContractV1>(ordinal);
                if (!handle) {
                    ordinalOrderMatches = false;
                    break;
                }
                const auto contribution = handle->tryBorrow();
                if (!contribution || contribution->get().owner != expectedContribution.owner ||
                    contribution->get().contributionId != expectedContribution.contributionId) {
                    ordinalOrderMatches = false;
                    break;
                }
            }

            const bool accessorsCalledExactlyOnce =
                syntheticContributionAccessorObservation(SyntheticFactoryV1::Root,
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
                        .invocationCount == 0 &&
                syntheticProjectOnlyInvocationCount() == 0;

            const ProcessScopeStopResultV2 stopped = executor->stop();
            return ordinalOrderMatches && accessorsCalledExactlyOnce && stopped &&
                   stopped->callbacksSucceeded() && syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool activeHandlesSurviveMoveAndRejectWrongThread() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor || !executor->start()) {
                return false;
            }
            ActiveExecutorStopGuardV1 sourceStopGuard{*executor};

            const auto registryResult = executor->contributions();
            if (!registryResult) {
                return false;
            }
            ProcessContributionRegistryViewV1 registry = *registryResult;
            const auto primaryHandle = registry.single<SyntheticPrimaryServiceContractV1>();
            if (!primaryHandle) {
                return false;
            }

            ProcessScopeExecutorV2 moved = std::move(*executor);
            ActiveExecutorStopGuardV1 movedStopGuard{moved};
            const auto movedFromContributions = executor->contributions();

            struct WrongThreadObservationV1 final {
                bool executorRejected{};
                bool registryRejected{};
                bool handleRejected{};
            } observation;
            std::jthread worker([&]() {
                observation.executorRejected =
                    hasLookupError(moved.contributions(),
                                   ProcessContributionLookupErrorCodeV1::WrongControlThread);
                observation.registryRejected =
                    hasLookupError(registry.size<SyntheticPrimaryServiceContractV1>(),
                                   ProcessContributionLookupErrorCodeV1::WrongControlThread);
                observation.handleRejected =
                    hasLookupError(primaryHandle->tryBorrow(),
                                   ProcessContributionLookupErrorCodeV1::WrongControlThread);
            });
            worker.join();

            const auto movedRegistry = moved.contributions();
            const auto mainThreadSize = registry.size<SyntheticPrimaryServiceContractV1>();
            const auto mainThreadBorrow = primaryHandle->tryBorrow();
            const bool mainThreadStillUsable =
                movedRegistry && mainThreadSize && *mainThreadSize == 1 && mainThreadBorrow &&
                mainThreadBorrow->get().owner == SyntheticFactoryV1::Root;
            const ProcessScopeStopResultV2 stopped = moved.stop();

            return hasLookupError(movedFromContributions,
                                  ProcessContributionLookupErrorCodeV1::ExecutorMovedFrom) &&
                   observation.executorRejected && observation.registryRejected &&
                   observation.handleRejected && mainThreadStillUsable && stopped &&
                   stopped->callbacksSucceeded() && syntheticProviderFixtureValid();
        }
        [[nodiscard]] bool lookupFailuresRemainTypedAndFailClosed() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor || !executor->start()) {
                return false;
            }
            ActiveExecutorStopGuardV1 stopGuard{*executor};

            const auto registryResult = executor->contributions();
            if (!registryResult) {
                return false;
            }
            const ProcessContributionRegistryViewV1& registry = *registryResult;
            const auto absent = registry.size<AbsentContributionContractV1>();
            const auto wrongType = registry.size<WrongTypeExtensionContractV1>();
            const auto wrongCardinality = registry.size<WrongCardinalityExtensionContractV1>();
            const auto outOfRange = registry.at<SyntheticExtensionContractV1>(3);

            const bool rejected =
                hasLookupError(absent, ProcessContributionLookupErrorCodeV1::ContractAbsent) &&
                hasLookupError(wrongType,
                               ProcessContributionLookupErrorCodeV1::ContractTypeMismatch) &&
                hasLookupError(wrongCardinality,
                               ProcessContributionLookupErrorCodeV1::ContractCardinalityMismatch) &&
                hasLookupError(outOfRange, ProcessContributionLookupErrorCodeV1::OrdinalOutOfRange);
            const ProcessScopeStopResultV2 stopped = executor->stop();
            return rejected && stopped && stopped->callbacksSucceeded() &&
                   syntheticProviderFixtureValid();
        }

        [[nodiscard]] bool singleConflictFailsBeforeAnyProviderCallback() {
            resetSyntheticProviderFixture();
            auto admitted = makeAdmittedSyntheticProcessScope(
                ProcessPlanMutationV1::IncludeProjectOnlySingleConflict);
            if (!admitted) {
                return false;
            }

            const auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (prepared) {
                return false;
            }
            const ProcessScopePreparationErrorV2& error = prepared.error();
            if (error.code != ProcessScopeErrorCodeV2::ContributionSingleConflict ||
                !error.factory || !error.contributionId || !error.contributionKind) {
                return false;
            }
            const ExactFactoryReferenceViewV1 factory = error.factory->view();
            return factory.packageId == kSyntheticPackageId &&
                   factory.packageVersion == kSyntheticPackageVersion &&
                   factory.moduleId == kSyntheticModuleId &&
                   factory.factoryId == kSyntheticProjectOnlyFactoryId &&
                   *error.contributionId == kSyntheticProjectOnlyPrimaryContributionId &&
                   *error.contributionKind == SyntheticPrimaryServiceContractV1::kind &&
                   !error.requirement && syntheticProviderTrace().empty() &&
                   syntheticProjectOnlyInvocationCount() == 0 &&
                   allSyntheticContributionAccessorsUnused() && syntheticProviderFixtureValid();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeContributionContractTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "contribution lookup cardinality is compile-time constrained",
                .function = &publicLookupCardinalityIsConstrainedAtCompileTime,
            },
            NamedProcessScopeTestV1{
                .name = "contributions are unavailable before start",
                .function = &contributionsAreUnavailableBeforeStart,
            },
            NamedProcessScopeTestV1{
                .name = "active registry publishes stable typed lookups",
                .function = &activeRegistryPublishesStableTypedLookups,
            },
            NamedProcessScopeTestV1{
                .name = "active contribution handles survive move and reject wrong thread",
                .function = &activeHandlesSurviveMoveAndRejectWrongThread,
            },
            NamedProcessScopeTestV1{
                .name = "contribution lookup failures remain typed",
                .function = &lookupFailuresRemainTypedAndFailClosed,
            },
            NamedProcessScopeTestV1{
                .name = "single contribution conflict fails before callbacks",
                .function = &singleConflictFailsBeforeAnyProviderCallback,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

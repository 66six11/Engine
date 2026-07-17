#include <array>
#include <thread>
#include <type_traits>
#include <utility>

#include "activation_eligibility_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] ActivationEligibilityResultV2<PendingActivationFactoryTableV2>
        makePendingTable(
            CurrentImageDescriptorMutationV2 mutation = CurrentImageDescriptorMutationV2::None) {
            auto admission = makePreRegistrationAdmission(mutation);
            if (!admission) {
                return std::unexpected(admission.error());
            }
            return recordAdmittedStaticFactoryProviders(std::move(*admission));
        }

        [[nodiscard]] bool activationApiRequiresPendingTypestate() {
            using ActivationFunction = decltype(&admitStaticFactoryActivation);
            static_assert(std::is_invocable_v<ActivationFunction, PendingActivationFactoryTableV2>);
            static_assert(!std::is_invocable_v<ActivationFunction, StaticFactoryCallbackTableV1>);
            static_assert(!std::is_constructible_v<PendingActivationFactoryTableV2,
                                                   StaticFactoryCallbackTableV1>);
            static_assert(!std::is_default_constructible_v<ActivationAdmissionV2>);
            static_assert(!std::is_copy_constructible_v<ActivationAdmissionV2>);
            static_assert(std::is_move_constructible_v<ActivationAdmissionV2>);
            static_assert(!std::is_move_assignable_v<ActivationAdmissionV2>);
            static_assert(!std::is_default_constructible_v<AdmittedStaticFactoryCallbackTableV2>);
            static_assert(!std::is_copy_constructible_v<AdmittedStaticFactoryCallbackTableV2>);
            static_assert(std::is_move_constructible_v<AdmittedStaticFactoryCallbackTableV2>);
            static_assert(!std::is_move_assignable_v<AdmittedStaticFactoryCallbackTableV2>);
            static_assert(!std::is_convertible_v<AdmittedStaticFactoryCallbackTableV2, bool>);
            return true;
        }

        [[nodiscard]] bool exactPendingTableProducesAdmittedDescriptorAccess() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            auto admitted = admitStaticFactoryActivation(std::move(*pending));
            const auto descriptorCount =
                admitted ? admittedDescriptorCount(*admitted) : std::nullopt;
            return admitted && admitted->registrationSnapshot().registrations.size() == 1 &&
                   descriptorCount && *descriptorCount == 1 &&
                   admittedTableUsesExpectedCallbacks(*admitted) &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        [[nodiscard]] bool zeroFactoryAdmissionReturnsEngagedEmptyView() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable(CurrentImageDescriptorMutationV2::ZeroFactoryFunction);
            if (!pending) {
                return false;
            }
            auto admitted = admitStaticFactoryActivation(std::move(*pending));
            if (!admitted) {
                return false;
            }
            const auto descriptorCount = admittedDescriptorCount(*admitted);
            return descriptorCount && *descriptorCount == 0 &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool evidenceOnlyOriginCannotBeUpgraded() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            PendingActivationFactoryTableV2 evidenceOnly =
                markPendingTableEvidenceOnly(std::move(*pending));
            const auto admitted = admitStaticFactoryActivation(std::move(evidenceOnly));
            return !admitted &&
                   admitted.error().stage == ActivationEligibilityStageV2::Activation &&
                   admitted.error().code == ActivationEligibilityErrorCodeV2::TableOriginInvalid &&
                   admitted.error().field == ActivationEligibilityFieldV2::TableOrigin &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool substitutedTableAddressFailsClosed() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            PendingActivationFactoryTableV2 corrupt =
                corruptPendingTableAddress(std::move(*pending));
            const auto admitted = admitStaticFactoryActivation(std::move(corrupt));
            return !admitted &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV2::TableInstanceMismatch &&
                   admitted.error().field == ActivationEligibilityFieldV2::TableInstance &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool equivalentReplacementTableFailsClosed() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            auto replaced = replacePendingWithEquivalentTable(std::move(*pending));
            if (!replaced) {
                return false;
            }
            const auto admitted = admitStaticFactoryActivation(std::move(*replaced));
            return !admitted &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV2::TableInstanceMismatch &&
                   admitted.error().field == ActivationEligibilityFieldV2::TableInstance &&
                   recordingFunctionInvocationCount() == 2 && providerInvocationCount() == 2 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool staleProcessEpochFailsActivation() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            rebindCurrentProcessEpochForTest();
            const auto admitted = admitStaticFactoryActivation(std::move(*pending));
            return !admitted &&
                   admitted.error().code == ActivationEligibilityErrorCodeV2::ProcessEpochStale &&
                   admitted.error().field == ActivationEligibilityFieldV2::CurrentProcess &&
                   lifecycleInvocationCount() == 0;
        }

        // Reusing the source is intentional: ownership transfer must invalidate
        // the original pending typestate.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedPendingTargetWorksAndSourceCannotBeReused() {
            resetEligibilityProbeCounts();
            auto pendingResult = makePendingTable();
            if (!pendingResult) {
                return false;
            }
            PendingActivationFactoryTableV2 pending = std::move(*pendingResult);
            PendingActivationFactoryTableV2 moved = std::move(pending);
            const auto admitted = admitStaticFactoryActivation(std::move(moved));
            if (!admitted || lifecycleInvocationCount() != 0) {
                return false;
            }

            const auto reused = admitStaticFactoryActivation(std::move(pending));
            return !reused &&
                   reused.error().code == ActivationEligibilityErrorCodeV2::PendingTableMovedFrom &&
                   lifecycleInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool wrongThreadConsumesPendingBeforeDescriptorAccess() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }

            ActivationEligibilityErrorV2 observed{};
            bool rejected = false;
            std::jthread worker([pending = std::move(*pending), &observed, &rejected]() mutable {
                const auto admitted = admitStaticFactoryActivation(std::move(pending));
                rejected = !admitted;
                if (!admitted) {
                    observed = admitted.error();
                }
            });
            worker.join();
            return rejected && observed.stage == ActivationEligibilityStageV2::Activation &&
                   observed.code == ActivationEligibilityErrorCodeV2::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV2::ControlThread &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        // Private white-box access verifies that moving the admitted wrapper
        // transfers authority rather than leaving an alias in the source.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedAdmittedSourceHasNoPrivateDescriptorAccess() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            auto admittedResult = admitStaticFactoryActivation(std::move(*pending));
            if (!admittedResult) {
                return false;
            }
            AdmittedStaticFactoryCallbackTableV2 admitted = std::move(*admittedResult);
            AdmittedStaticFactoryCallbackTableV2 moved = std::move(admitted);
            const auto sourceCount = admittedDescriptorCount(admitted);
            const auto targetCount = admittedDescriptorCount(moved);
            return !sourceCount && targetCount && *targetCount == 1 &&
                   lifecycleInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool wrongThreadCannotReadAdmittedDescriptors() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            auto admitted = admitStaticFactoryActivation(std::move(*pending));
            if (!admitted) {
                return false;
            }

            std::optional<std::size_t> observedCount;
            std::jthread worker([admitted = std::move(*admitted), &observedCount]() mutable {
                observedCount = admittedDescriptorCount(admitted);
            });
            worker.join();
            return !observedCount && lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool staleProcessCannotReadAdmittedDescriptors() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            auto admitted = admitStaticFactoryActivation(std::move(*pending));
            if (!admitted) {
                return false;
            }
            rebindCurrentProcessEpochForTest();
            return !admittedDescriptorCount(*admitted) && lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool byteIdenticalEvidenceTablesRemainOutsideActivationApi() {
            resetEligibilityProbeCounts();
            const auto expected = collectEvidenceOnlyTable(false);
            const auto alternate = collectEvidenceOnlyTable(true);
            return expected && alternate &&
                   expected->registrationSnapshot() == alternate->registrationSnapshot() &&
                   recordingFunctionInvocationCount() == 2 && providerInvocationCount() == 2 &&
                   lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV2> activationAdmissionTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV2{
                .name = "activation API requires V2 pending typestate",
                .function = &activationApiRequiresPendingTypestate,
            },
            NamedEligibilityTestV2{
                .name = "exact pending table admits private descriptors",
                .function = &exactPendingTableProducesAdmittedDescriptorAccess,
            },
            NamedEligibilityTestV2{
                .name = "zero-factory admission has an engaged empty view",
                .function = &zeroFactoryAdmissionReturnsEngagedEmptyView,
            },
            NamedEligibilityTestV2{
                .name = "evidence-only origin cannot be upgraded",
                .function = &evidenceOnlyOriginCannotBeUpgraded,
            },
            NamedEligibilityTestV2{
                .name = "substituted table address fails",
                .function = &substitutedTableAddressFailsClosed,
            },
            NamedEligibilityTestV2{
                .name = "same-address replacement table fails",
                .function = &equivalentReplacementTableFailsClosed,
            },
            NamedEligibilityTestV2{
                .name = "stale process epoch fails activation",
                .function = &staleProcessEpochFailsActivation,
            },
            NamedEligibilityTestV2{
                .name = "moved pending cannot be reused",
                .function = &movedPendingTargetWorksAndSourceCannotBeReused,
            },
            NamedEligibilityTestV2{
                .name = "wrong thread consumes pending",
                .function = &wrongThreadConsumesPendingBeforeDescriptorAccess,
            },
            NamedEligibilityTestV2{
                .name = "moved admitted source has no private access",
                .function = &movedAdmittedSourceHasNoPrivateDescriptorAccess,
            },
            NamedEligibilityTestV2{
                .name = "wrong thread has no admitted descriptor access",
                .function = &wrongThreadCannotReadAdmittedDescriptors,
            },
            NamedEligibilityTestV2{
                .name = "stale process has no admitted descriptor access",
                .function = &staleProcessCannotReadAdmittedDescriptors,
            },
            NamedEligibilityTestV2{
                .name = "equivalent evidence tables stay outside activation",
                .function = &byteIdenticalEvidenceTablesRemainOutsideActivationApi,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

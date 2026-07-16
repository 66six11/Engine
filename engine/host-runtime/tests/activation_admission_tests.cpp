#include "activation_eligibility_test_support.hpp"

#include <array>
#include <thread>
#include <type_traits>
#include <utility>

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] ActivationEligibilityResultV1<PendingActivationFactoryTableV1>
        makePendingTable(EligibilityHandoffMutationV1 mutation =
                             EligibilityHandoffMutationV1::None) {
            auto admission = makePreRegistrationAdmission(mutation);
            if (!admission) {
                return std::unexpected(admission.error());
            }
            return recordAdmittedStaticFactoryProviders(std::move(*admission));
        }

        [[nodiscard]] bool activationApiRequiresPendingTypestate() {
            using ActivationFunction = decltype(&admitStaticFactoryActivation);
            static_assert(std::is_invocable_v<ActivationFunction,
                                              PendingActivationFactoryTableV1>);
            static_assert(!std::is_invocable_v<ActivationFunction,
                                               StaticFactoryCallbackTableV1>);
            static_assert(!std::is_constructible_v<PendingActivationFactoryTableV1,
                                                   StaticFactoryCallbackTableV1>);
            static_assert(!std::is_default_constructible_v<ActivationAdmissionV1>);
            static_assert(!std::is_copy_constructible_v<ActivationAdmissionV1>);
            static_assert(std::is_move_constructible_v<ActivationAdmissionV1>);
            static_assert(!std::is_move_assignable_v<ActivationAdmissionV1>);
            static_assert(!std::is_default_constructible_v<
                          AdmittedStaticFactoryCallbackTableV1>);
            static_assert(!std::is_copy_constructible_v<
                          AdmittedStaticFactoryCallbackTableV1>);
            static_assert(std::is_move_constructible_v<
                          AdmittedStaticFactoryCallbackTableV1>);
            static_assert(!std::is_move_assignable_v<
                          AdmittedStaticFactoryCallbackTableV1>);
            static_assert(!std::is_convertible_v<
                          AdmittedStaticFactoryCallbackTableV1, bool>);
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
            auto pending = makePendingTable(EligibilityHandoffMutationV1::ZeroFactoryFunction);
            if (!pending) {
                return false;
            }
            auto admitted = admitStaticFactoryActivation(std::move(*pending));
            if (!admitted) {
                return false;
            }
            const auto descriptorCount = admittedDescriptorCount(*admitted);
            return descriptorCount && *descriptorCount == 0 &&
                   recordingFunctionInvocationCount() == 1 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool unexpectedSnapshotCannotProduceActivationAdmission() {
            resetEligibilityProbeCounts();
            auto pending =
                makePendingTable(EligibilityHandoffMutationV1::UnexpectedSnapshotFunction);
            if (!pending) {
                return false;
            }
            const auto admitted = admitStaticFactoryActivation(std::move(*pending));
            return !admitted &&
                   admitted.error().stage == ActivationEligibilityStageV1::Activation &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV1::TableSnapshotMismatch &&
                   admitted.error().field == ActivationEligibilityFieldV1::TableSnapshot &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        [[nodiscard]] bool corruptedExpectedSnapshotFailsClosed() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            PendingActivationFactoryTableV1 corrupt =
                corruptPendingExpectedSnapshot(std::move(*pending));
            const auto admitted = admitStaticFactoryActivation(std::move(corrupt));
            return !admitted &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV1::TableSnapshotMismatch &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool evidenceOnlyOriginCannotBeUpgraded() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            PendingActivationFactoryTableV1 evidenceOnly =
                markPendingTableEvidenceOnly(std::move(*pending));
            const auto admitted = admitStaticFactoryActivation(std::move(evidenceOnly));
            return !admitted &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV1::TableOriginInvalid &&
                   admitted.error().field == ActivationEligibilityFieldV1::TableOrigin &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool substitutedTableAddressFailsClosed() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }
            PendingActivationFactoryTableV1 corrupt =
                corruptPendingTableAddress(std::move(*pending));
            const auto admitted = admitStaticFactoryActivation(std::move(corrupt));
            return !admitted &&
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV1::TableInstanceMismatch &&
                   admitted.error().field == ActivationEligibilityFieldV1::TableInstance &&
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
                       ActivationEligibilityErrorCodeV1::TableInstanceMismatch &&
                   admitted.error().field == ActivationEligibilityFieldV1::TableInstance &&
                   recordingFunctionInvocationCount() == 2 &&
                   providerInvocationCount() == 2 && lifecycleInvocationCount() == 0;
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
                   admitted.error().code ==
                       ActivationEligibilityErrorCodeV1::ProcessEpochStale &&
                   admitted.error().field ==
                       ActivationEligibilityFieldV1::CurrentProcess &&
                   lifecycleInvocationCount() == 0;
        }

        // Reusing the source is intentional here: the pending typestate must
        // reject a second admission attempt after ownership was transferred.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedPendingTargetWorksAndSourceCannotBeReused() {
            resetEligibilityProbeCounts();
            auto pendingResult = makePendingTable();
            if (!pendingResult) {
                return false;
            }
            PendingActivationFactoryTableV1 pending = std::move(*pendingResult);
            PendingActivationFactoryTableV1 moved = std::move(pending);
            const auto admitted = admitStaticFactoryActivation(std::move(moved));
            if (!admitted || lifecycleInvocationCount() != 0) {
                return false;
            }

            const auto reused = admitStaticFactoryActivation(std::move(pending));
            return !reused &&
                   reused.error().code ==
                       ActivationEligibilityErrorCodeV1::PendingTableMovedFrom &&
                   lifecycleInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool wrongThreadConsumesPendingBeforeDescriptorAccess() {
            resetEligibilityProbeCounts();
            auto pending = makePendingTable();
            if (!pending) {
                return false;
            }

            ActivationEligibilityErrorV1 observed{};
            bool rejected = false;
            std::jthread worker([pending = std::move(*pending), &observed, &rejected]() mutable {
                const auto admitted = admitStaticFactoryActivation(std::move(pending));
                rejected = !admitted;
                if (!admitted) {
                    observed = admitted.error();
                }
            });
            worker.join();
            return rejected && observed.stage == ActivationEligibilityStageV1::Activation &&
                   observed.code == ActivationEligibilityErrorCodeV1::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV1::ControlThread &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        // Private white-box access is used only to verify that moving the admitted
        // wrapper transfers the callback table instead of aliasing it.
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
            AdmittedStaticFactoryCallbackTableV1 admitted = std::move(*admittedResult);
            AdmittedStaticFactoryCallbackTableV1 moved = std::move(admitted);
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
            std::jthread worker(
                [admitted = std::move(*admitted), &observedCount]() mutable {
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
                   recordingFunctionInvocationCount() == 2 &&
                   providerInvocationCount() == 2 && lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV1> activationAdmissionTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV1{
                .name = "activation API requires pending typestate",
                .function = &activationApiRequiresPendingTypestate,
            },
            NamedEligibilityTestV1{
                .name = "exact pending table admits private descriptors",
                .function = &exactPendingTableProducesAdmittedDescriptorAccess,
            },
            NamedEligibilityTestV1{
                .name = "zero-factory admission has an engaged empty view",
                .function = &zeroFactoryAdmissionReturnsEngagedEmptyView,
            },
            NamedEligibilityTestV1{
                .name = "unexpected snapshot fails activation",
                .function = &unexpectedSnapshotCannotProduceActivationAdmission,
            },
            NamedEligibilityTestV1{
                .name = "corrupted expected snapshot fails",
                .function = &corruptedExpectedSnapshotFailsClosed,
            },
            NamedEligibilityTestV1{
                .name = "evidence-only origin cannot be upgraded",
                .function = &evidenceOnlyOriginCannotBeUpgraded,
            },
            NamedEligibilityTestV1{
                .name = "substituted table address fails",
                .function = &substitutedTableAddressFailsClosed,
            },
            NamedEligibilityTestV1{
                .name = "same-address equivalent replacement table fails",
                .function = &equivalentReplacementTableFailsClosed,
            },
            NamedEligibilityTestV1{
                .name = "stale process epoch fails activation",
                .function = &staleProcessEpochFailsActivation,
            },
            NamedEligibilityTestV1{
                .name = "moved pending cannot be reused",
                .function = &movedPendingTargetWorksAndSourceCannotBeReused,
            },
            NamedEligibilityTestV1{
                .name = "wrong thread consumes pending",
                .function = &wrongThreadConsumesPendingBeforeDescriptorAccess,
            },
            NamedEligibilityTestV1{
                .name = "moved admitted source has no private access",
                .function = &movedAdmittedSourceHasNoPrivateDescriptorAccess,
            },
            NamedEligibilityTestV1{
                .name = "wrong thread has no admitted descriptor access",
                .function = &wrongThreadCannotReadAdmittedDescriptors,
            },
            NamedEligibilityTestV1{
                .name = "stale process has no admitted descriptor access",
                .function = &staleProcessCannotReadAdmittedDescriptors,
            },
            NamedEligibilityTestV1{
                .name = "equivalent evidence tables stay outside activation",
                .function = &byteIdenticalEvidenceTablesRemainOutsideActivationApi,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

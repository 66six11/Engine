#include <array>
#include <thread>
#include <type_traits>
#include <utility>

#include "activation_eligibility_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] bool recordingApiConsumesOnlyPreAdmission() {
            using RecordingFunction = decltype(&recordAdmittedStaticFactoryProviders);
            static_assert(std::is_invocable_v<RecordingFunction, PreRegistrationAdmissionV2>);
            static_assert(!std::is_invocable_v<RecordingFunction, PreRegistrationAdmissionV2,
                                               StaticFactoryRegistrationCapacityV2,
                                               StaticFactoryRecordingFunctionV2>);
            static_assert(!std::is_invocable_v<RecordingFunction, StaticFactoryCallbackTableV1>);

            static_assert(!std::is_default_constructible_v<PendingActivationFactoryTableV2>);
            static_assert(!std::is_copy_constructible_v<PendingActivationFactoryTableV2>);
            static_assert(std::is_move_constructible_v<PendingActivationFactoryTableV2>);
            static_assert(!std::is_move_assignable_v<PendingActivationFactoryTableV2>);
            return true;
        }

        [[nodiscard]] bool validAdmissionRecordsExpectedProvidersExactlyOnce() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission();
            if (!admission) {
                return false;
            }
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return pending && recordingFunctionInvocationCount() == 1 &&
                   providerInvocationCount() == 1 && lifecycleInvocationCount() == 0 &&
                   contributionAccessorInvocationCount() == 0;
        }

        // Reusing the source is intentional: the move target owns the only valid
        // registration authority and the source must fail closed.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedAdmissionTargetWorksAndSourceCannotBeReused() {
            resetEligibilityProbeCounts();
            auto admissionResult = makePreRegistrationAdmission();
            if (!admissionResult) {
                return false;
            }
            PreRegistrationAdmissionV2 admission = std::move(*admissionResult);
            PreRegistrationAdmissionV2 moved = std::move(admission);
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(moved));
            if (!pending || recordingFunctionInvocationCount() != 1 ||
                providerInvocationCount() != 1 || lifecycleInvocationCount() != 0) {
                return false;
            }

            const auto reused = recordAdmittedStaticFactoryProviders(std::move(admission));
            return !reused &&
                   reused.error().stage == ActivationEligibilityStageV2::ProviderRecording &&
                   reused.error().code == ActivationEligibilityErrorCodeV2::AdmissionMovedFrom &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool invalidCapacityConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission(
                CurrentImageDescriptorMutationV2::InvalidCapacityFunction);
            if (!admission) {
                return false;
            }
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage == ActivationEligibilityStageV2::ProviderRecording &&
                   pending.error().code == ActivationEligibilityErrorCodeV2::RegistrationFailed &&
                   pending.error().registrationCode ==
                       StaticFactoryRegistrationErrorCode::InvalidCapacity &&
                   recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool providerRegistrationFailureReturnsNoPendingTable() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission(
                CurrentImageDescriptorMutationV2::RegistrationFailureFunction);
            if (!admission) {
                return false;
            }
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage == ActivationEligibilityStageV2::ProviderRecording &&
                   pending.error().code == ActivationEligibilityErrorCodeV2::RegistrationFailed &&
                   pending.error().registrationCode ==
                       StaticFactoryRegistrationErrorCode::FactoryMissing &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        [[nodiscard]] bool unexpectedSnapshotHeaderReturnsNoPendingTable() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission(
                CurrentImageDescriptorMutationV2::UnexpectedSnapshotFunction);
            if (!admission) {
                return false;
            }
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage == ActivationEligibilityStageV2::ProviderRecording &&
                   pending.error().code ==
                       ActivationEligibilityErrorCodeV2::TableSnapshotMismatch &&
                   pending.error().field == ActivationEligibilityFieldV2::TableSnapshot &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool wrongThreadConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission();
            if (!admission) {
                return false;
            }

            ActivationEligibilityErrorV2 observed{};
            bool rejected = false;
            std::jthread worker(
                [admission = std::move(*admission), &observed, &rejected]() mutable {
                    const auto pending = recordAdmittedStaticFactoryProviders(std::move(admission));
                    rejected = !pending;
                    if (!pending) {
                        observed = pending.error();
                    }
                });
            worker.join();
            return rejected && observed.stage == ActivationEligibilityStageV2::ProviderRecording &&
                   observed.code == ActivationEligibilityErrorCodeV2::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV2::ControlThread &&
                   recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool staleProcessConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission();
            if (!admission) {
                return false;
            }
            rebindCurrentProcessEpochForTest();
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage == ActivationEligibilityStageV2::ProviderRecording &&
                   pending.error().code == ActivationEligibilityErrorCodeV2::ProcessEpochStale &&
                   pending.error().field == ActivationEligibilityFieldV2::CurrentProcess &&
                   recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV2> admittedStaticFactoryRecordingTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV2{
                .name = "recording API consumes only V2 pre-admission",
                .function = &recordingApiConsumesOnlyPreAdmission,
            },
            NamedEligibilityTestV2{
                .name = "valid admission records providers once",
                .function = &validAdmissionRecordsExpectedProvidersExactlyOnce,
            },
            NamedEligibilityTestV2{
                .name = "moved admission cannot be reused",
                .function = &movedAdmissionTargetWorksAndSourceCannotBeReused,
            },
            NamedEligibilityTestV2{
                .name = "invalid capacity consumes admission",
                .function = &invalidCapacityConsumesAdmissionBeforeDriver,
            },
            NamedEligibilityTestV2{
                .name = "provider failure returns no pending table",
                .function = &providerRegistrationFailureReturnsNoPendingTable,
            },
            NamedEligibilityTestV2{
                .name = "wrong snapshot header returns no pending table",
                .function = &unexpectedSnapshotHeaderReturnsNoPendingTable,
            },
            NamedEligibilityTestV2{
                .name = "wrong thread consumes admission",
                .function = &wrongThreadConsumesAdmissionBeforeDriver,
            },
            NamedEligibilityTestV2{
                .name = "stale process consumes admission",
                .function = &staleProcessConsumesAdmissionBeforeDriver,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

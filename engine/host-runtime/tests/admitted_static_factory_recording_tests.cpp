#include "activation_eligibility_test_support.hpp"

#include <array>
#include <thread>
#include <type_traits>
#include <utility>

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] bool recordingApiConsumesOnlyPreAdmission() {
            using RecordingFunction = decltype(&recordAdmittedStaticFactoryProviders);
            static_assert(std::is_invocable_v<RecordingFunction,
                                              PreRegistrationAdmissionV1>);
            static_assert(!std::is_invocable_v<RecordingFunction,
                                               PreRegistrationAdmissionV1,
                                               StaticFactoryRegistrationCapacityV2,
                                               StaticFactoryRecordingFunctionV1>);
            static_assert(!std::is_invocable_v<RecordingFunction,
                                               StaticFactoryCallbackTableV1>);

            static_assert(!std::is_default_constructible_v<
                          PendingActivationFactoryTableV1>);
            static_assert(!std::is_copy_constructible_v<
                          PendingActivationFactoryTableV1>);
            static_assert(std::is_move_constructible_v<
                          PendingActivationFactoryTableV1>);
            static_assert(!std::is_move_assignable_v<
                          PendingActivationFactoryTableV1>);
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

        // Reusing the source is intentional here: the public contract defines a
        // moved-from admission as an explicit fail-closed input.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedAdmissionTargetWorksAndSourceCannotBeReused() {
            resetEligibilityProbeCounts();
            auto admissionResult = makePreRegistrationAdmission();
            if (!admissionResult) {
                return false;
            }
            PreRegistrationAdmissionV1 admission = std::move(*admissionResult);
            PreRegistrationAdmissionV1 moved = std::move(admission);
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(moved));
            if (!pending || recordingFunctionInvocationCount() != 1 ||
                providerInvocationCount() != 1 || lifecycleInvocationCount() != 0) {
                return false;
            }

            const auto reused = recordAdmittedStaticFactoryProviders(std::move(admission));
            return !reused &&
                   reused.error().stage == ActivationEligibilityStageV1::ProviderRecording &&
                   reused.error().code == ActivationEligibilityErrorCodeV1::AdmissionMovedFrom &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool invalidCapacityConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission =
                makePreRegistrationAdmission(EligibilityHandoffMutationV1::InvalidCapacityFunction);
            if (!admission) {
                return false;
            }
            const auto pending =
                recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage ==
                       ActivationEligibilityStageV1::ProviderRecording &&
                   pending.error().code ==
                       ActivationEligibilityErrorCodeV1::RegistrationFailed &&
                   pending.error().registrationCode ==
                       StaticFactoryRegistrationErrorCode::InvalidCapacity &&
                   recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool providerRegistrationFailureReturnsNoPendingTable() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission(
                EligibilityHandoffMutationV1::RegistrationFailureFunction);
            if (!admission) {
                return false;
            }
            const auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage == ActivationEligibilityStageV1::ProviderRecording &&
                   pending.error().code == ActivationEligibilityErrorCodeV1::RegistrationFailed &&
                   pending.error().registrationCode ==
                       StaticFactoryRegistrationErrorCode::FactoryMissing &&
                   recordingFunctionInvocationCount() == 1 && providerInvocationCount() == 1 &&
                   lifecycleInvocationCount() == 0 && contributionAccessorInvocationCount() == 0;
        }

        [[nodiscard]] bool wrongThreadConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission();
            if (!admission) {
                return false;
            }

            ActivationEligibilityErrorV1 observed{};
            bool rejected = false;
            std::jthread worker(
                [admission = std::move(*admission), &observed, &rejected]() mutable {
                    const auto pending =
                        recordAdmittedStaticFactoryProviders(std::move(admission));
                    rejected = !pending;
                    if (!pending) {
                        observed = pending.error();
                    }
                });
            worker.join();
            return rejected &&
                   observed.stage == ActivationEligibilityStageV1::ProviderRecording &&
                   observed.code == ActivationEligibilityErrorCodeV1::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV1::ControlThread &&
                   recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool staleProcessConsumesAdmissionBeforeDriver() {
            resetEligibilityProbeCounts();
            auto admission = makePreRegistrationAdmission();
            if (!admission) {
                return false;
            }
            rebindCurrentProcessEpochForTest();
            const auto pending =
                recordAdmittedStaticFactoryProviders(std::move(*admission));
            return !pending &&
                   pending.error().stage ==
                       ActivationEligibilityStageV1::ProviderRecording &&
                   pending.error().code ==
                       ActivationEligibilityErrorCodeV1::ProcessEpochStale &&
                   pending.error().field ==
                       ActivationEligibilityFieldV1::CurrentProcess &&
                   recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV1>
    admittedStaticFactoryRecordingTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV1{
                .name = "recording API consumes only pre-admission",
                .function = &recordingApiConsumesOnlyPreAdmission,
            },
            NamedEligibilityTestV1{
                .name = "valid admission records providers once",
                .function = &validAdmissionRecordsExpectedProvidersExactlyOnce,
            },
            NamedEligibilityTestV1{
                .name = "moved admission cannot be reused",
                .function = &movedAdmissionTargetWorksAndSourceCannotBeReused,
            },
            NamedEligibilityTestV1{
                .name = "invalid capacity consumes admission",
                .function = &invalidCapacityConsumesAdmissionBeforeDriver,
            },
            NamedEligibilityTestV1{
                .name = "provider failure returns no pending table",
                .function = &providerRegistrationFailureReturnsNoPendingTable,
            },
            NamedEligibilityTestV1{
                .name = "wrong thread consumes admission",
                .function = &wrongThreadConsumesAdmissionBeforeDriver,
            },
            NamedEligibilityTestV1{
                .name = "stale process consumes admission",
                .function = &staleProcessConsumesAdmissionBeforeDriver,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

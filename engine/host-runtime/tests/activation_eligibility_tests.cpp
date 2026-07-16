#include "activation_eligibility_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] bool sealedTypesAreLinear() {
            static_assert(!std::is_default_constructible_v<ReadySessionHandoffV1>);
            static_assert(!std::is_copy_constructible_v<ReadySessionHandoffV1>);
            static_assert(std::is_move_constructible_v<ReadySessionHandoffV1>);
            static_assert(!std::is_move_assignable_v<ReadySessionHandoffV1>);

            static_assert(!std::is_default_constructible_v<
                          VerifiedHostActivationBlueprintHandoffV1>);
            static_assert(!std::is_copy_constructible_v<
                          VerifiedHostActivationBlueprintHandoffV1>);
            static_assert(std::is_move_constructible_v<
                          VerifiedHostActivationBlueprintHandoffV1>);
            static_assert(!std::is_move_assignable_v<
                          VerifiedHostActivationBlueprintHandoffV1>);

            static_assert(!std::is_default_constructible_v<DeepVerifiedHostBindingHandoffV1>);
            static_assert(!std::is_copy_constructible_v<DeepVerifiedHostBindingHandoffV1>);
            static_assert(std::is_move_constructible_v<DeepVerifiedHostBindingHandoffV1>);
            static_assert(!std::is_move_assignable_v<DeepVerifiedHostBindingHandoffV1>);

            static_assert(!std::is_default_constructible_v<
                          VerifiedCurrentProcessLaunchHandoffV1>);
            static_assert(!std::is_copy_constructible_v<
                          VerifiedCurrentProcessLaunchHandoffV1>);
            static_assert(std::is_move_constructible_v<
                          VerifiedCurrentProcessLaunchHandoffV1>);
            static_assert(!std::is_move_assignable_v<
                          VerifiedCurrentProcessLaunchHandoffV1>);

            static_assert(!std::is_default_constructible_v<PreRegistrationAdmissionV1>);
            static_assert(!std::is_copy_constructible_v<PreRegistrationAdmissionV1>);
            static_assert(std::is_move_constructible_v<PreRegistrationAdmissionV1>);
            static_assert(!std::is_move_assignable_v<PreRegistrationAdmissionV1>);
            static_assert(!std::is_convertible_v<PreRegistrationAdmissionV1, bool>);
            return true;
        }

        [[nodiscard]] bool validLineageProducesOneAdmissionWithoutInvokingCode() {
            resetEligibilityProbeCounts();
            const auto result = makePreRegistrationAdmission();
            return result && recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

        // Reusing the source is intentional here: consuming one handoff must leave
        // an observable fail-closed state for a second admission attempt.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedFromHandoffFailsClosed() {
            resetEligibilityProbeCounts();
            EligibilityHandoffsV1 handoffs = makeEligibilityHandoffs();
            [[maybe_unused]] ReadySessionHandoffV1 consumed =
                std::move(handoffs.readySession);

            const auto result = admitPreRegistration(
                std::move(handoffs.readySession), std::move(handoffs.blueprint),
                std::move(handoffs.binding), std::move(handoffs.launchHandoff));
            return !result &&
                   result.error().stage == ActivationEligibilityStageV1::PreRegistration &&
                   result.error().code ==
                       ActivationEligibilityErrorCodeV1::HandoffMovedFrom &&
                   result.error().field == ActivationEligibilityFieldV1::ReadySession &&
                   recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool lineageMismatchMatrixFailsBeforeProviderCode() {
            struct Case final {
                EligibilityHandoffMutationV1 mutation;
                ActivationEligibilityErrorCodeV1 code;
                ActivationEligibilityFieldV1 field;
            };
            constexpr std::array cases{
                Case{
                    .mutation = EligibilityHandoffMutationV1::InvalidReadySession,
                    .code = ActivationEligibilityErrorCodeV1::ReadySessionInvalid,
                    .field = ActivationEligibilityFieldV1::ReadySession,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::InvalidBinding,
                    .code = ActivationEligibilityErrorCodeV1::BindingInvalid,
                    .field = ActivationEligibilityFieldV1::Binding,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::SessionFingerprintMismatch,
                    .code = ActivationEligibilityErrorCodeV1::EffectiveSessionMismatch,
                    .field = ActivationEligibilityFieldV1::EffectiveSessionIntegrity,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::HostIdentityMismatch,
                    .code = ActivationEligibilityErrorCodeV1::HostIdentityMismatch,
                    .field = ActivationEligibilityFieldV1::HostIdentity,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::BlueprintMismatch,
                    .code = ActivationEligibilityErrorCodeV1::BlueprintMismatch,
                    .field = ActivationEligibilityFieldV1::BlueprintIntegrity,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::StaticCompositionMismatch,
                    .code = ActivationEligibilityErrorCodeV1::StaticCompositionMismatch,
                    .field = ActivationEligibilityFieldV1::StaticComposition,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::HostTemplateMismatch,
                    .code = ActivationEligibilityErrorCodeV1::HostTemplateMismatch,
                    .field = ActivationEligibilityFieldV1::HostTemplate,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::UnsupportedTemplateRenderer,
                    .code = ActivationEligibilityErrorCodeV1::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV1::GenerationTuple,
                },
                Case{
                    .mutation =
                        EligibilityHandoffMutationV1::UnsupportedCompositionRenderer,
                    .code = ActivationEligibilityErrorCodeV1::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV1::GenerationTuple,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::UnsupportedProviderApi,
                    .code = ActivationEligibilityErrorCodeV1::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV1::GenerationTuple,
                },
                Case{
                    .mutation =
                        EligibilityHandoffMutationV1::UnsupportedSnapshotSchemaVersion,
                    .code = ActivationEligibilityErrorCodeV1::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV1::GenerationTuple,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::BindingGenerationMismatch,
                    .code = ActivationEligibilityErrorCodeV1::BindingGenerationMismatch,
                    .field = ActivationEligibilityFieldV1::BindingGeneration,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::ArtifactMismatch,
                    .code = ActivationEligibilityErrorCodeV1::ArtifactMismatch,
                    .field = ActivationEligibilityFieldV1::ArtifactIdentity,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::ExpectedSnapshotInvalid,
                    .code = ActivationEligibilityErrorCodeV1::ExpectedSnapshotInvalid,
                    .field = ActivationEligibilityFieldV1::ExpectedSnapshot,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::LaunchProcessEpochMissing,
                    .code = ActivationEligibilityErrorCodeV1::LaunchHandoffInvalid,
                    .field = ActivationEligibilityFieldV1::LaunchHandoff,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::LaunchProcessEpochStale,
                    .code = ActivationEligibilityErrorCodeV1::ProcessEpochStale,
                    .field = ActivationEligibilityFieldV1::CurrentProcess,
                },
                Case{
                    .mutation = EligibilityHandoffMutationV1::LaunchProcessEpochConsumed,
                    .code = ActivationEligibilityErrorCodeV1::ProcessEpochConsumed,
                    .field = ActivationEligibilityFieldV1::CurrentProcess,
                },
                Case{
                    .mutation =
                        EligibilityHandoffMutationV1::LaunchControlThreadEpochMissing,
                    .code = ActivationEligibilityErrorCodeV1::LaunchHandoffInvalid,
                    .field = ActivationEligibilityFieldV1::LaunchHandoff,
                },
                Case{
                    .mutation =
                        EligibilityHandoffMutationV1::LaunchRecordingFunctionMissing,
                    .code = ActivationEligibilityErrorCodeV1::LaunchHandoffInvalid,
                    .field = ActivationEligibilityFieldV1::LaunchHandoff,
                },
            };

            return std::ranges::all_of(cases, [](const Case& testCase) {
                resetEligibilityProbeCounts();
                const auto result = makePreRegistrationAdmission(testCase.mutation);
                return !result &&
                       result.error().stage ==
                           ActivationEligibilityStageV1::PreRegistration &&
                       result.error().code == testCase.code &&
                       result.error().field == testCase.field &&
                       recordingFunctionInvocationCount() == 0 &&
                       providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
            });
        }

        [[nodiscard]] bool wrongThreadFailsAndConsumesAllHandoffs() {
            resetEligibilityProbeCounts();
            EligibilityHandoffsV1 handoffs = makeEligibilityHandoffs();
            ActivationEligibilityErrorV1 observed{};
            bool rejected = false;
            std::jthread worker(
                [handoffs = std::move(handoffs), &observed, &rejected]() mutable {
                    const auto result = admitPreRegistration(
                        std::move(handoffs.readySession), std::move(handoffs.blueprint),
                        std::move(handoffs.binding), std::move(handoffs.launchHandoff));
                    rejected = !result;
                    if (!result) {
                        observed = result.error();
                    }
                });
            worker.join();
            return rejected && observed.stage == ActivationEligibilityStageV1::PreRegistration &&
                   observed.code == ActivationEligibilityErrorCodeV1::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV1::ControlThread &&
                   recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV1> preRegistrationEligibilityTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV1{
                .name = "sealed types are linear",
                .function = &sealedTypesAreLinear,
            },
            NamedEligibilityTestV1{
                .name = "valid lineage produces one admission",
                .function = &validLineageProducesOneAdmissionWithoutInvokingCode,
            },
            NamedEligibilityTestV1{
                .name = "moved-from handoff fails closed",
                .function = &movedFromHandoffFailsClosed,
            },
            NamedEligibilityTestV1{
                .name = "lineage mismatch matrix fails before provider code",
                .function = &lineageMismatchMatrixFailsBeforeProviderCode,
            },
            NamedEligibilityTestV1{
                .name = "wrong thread consumes handoffs",
                .function = &wrongThreadFailsAndConsumesAllHandoffs,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

int main() {
    using asharia::host_runtime::tests::NamedEligibilityTestV1;
    try {
        const std::array groups{
            asharia::host_runtime::tests::preRegistrationEligibilityTests(),
            asharia::host_runtime::tests::admittedStaticFactoryRecordingTests(),
            asharia::host_runtime::tests::activationAdmissionTests(),
        };

        const bool succeeded = std::ranges::all_of(
            groups, [](const std::span<const NamedEligibilityTestV1> group) {
                return std::ranges::all_of(group, [](const NamedEligibilityTestV1& test) {
                    if (test.function()) {
                        return true;
                    }
                    constexpr std::string_view prefix{"FAILED: "};
                    constexpr std::string_view newline{"\n"};
                    (void)std::fwrite(prefix.data(), 1, prefix.size(), stderr);
                    (void)std::fwrite(test.name.data(), 1, test.name.size(), stderr);
                    (void)std::fwrite(newline.data(), 1, newline.size(), stderr);
                    return false;
                });
            });
        return succeeded ? 0 : 1;
    } catch (...) {
        constexpr std::string_view message{"FAILED: unexpected test exception\n"};
        (void)std::fwrite(message.data(), 1, message.size(), stderr);
        return 1;
    }
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

#include "activation_eligibility_test_support.hpp"

#if __has_include("asharia/host_runtime/current_image_activation_descriptor_provider_access.hpp")
#error "The current-image provider bridge must not propagate to ordinary test consumers."
#endif

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] bool sealedTypesAreLinear() {
            static_assert(!std::is_default_constructible_v<CurrentImageActivationDescriptorV2>);
            static_assert(!std::is_copy_constructible_v<CurrentImageActivationDescriptorV2>);
            static_assert(std::is_move_constructible_v<CurrentImageActivationDescriptorV2>);
            static_assert(!std::is_move_assignable_v<CurrentImageActivationDescriptorV2>);
            static_assert(!std::is_convertible_v<CurrentImageActivationDescriptorV2, bool>);

            static_assert(!std::is_default_constructible_v<PreRegistrationAdmissionV2>);
            static_assert(!std::is_copy_constructible_v<PreRegistrationAdmissionV2>);
            static_assert(std::is_move_constructible_v<PreRegistrationAdmissionV2>);
            static_assert(!std::is_move_assignable_v<PreRegistrationAdmissionV2>);
            static_assert(!std::is_convertible_v<PreRegistrationAdmissionV2, bool>);
            return true;
        }

        [[nodiscard]] bool validDescriptorProducesOneAdmissionWithoutInvokingCode() {
            resetEligibilityProbeCounts();
            const auto result = makePreRegistrationAdmission();
            return result && recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0 &&
                   contributionAccessorInvocationCount() == 0;
        }

        // Reusing the source is intentional: a moved descriptor must fail closed,
        // while the move target remains the sole authority-bearing value.
        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedDescriptorSourceFailsClosed() {
            resetEligibilityProbeCounts();
            resetCurrentImageEpochForTest();
            auto issued = issueCurrentImageActivationDescriptor();
            if (!issued) {
                return false;
            }

            CurrentImageActivationDescriptorV2 descriptor = std::move(*issued);
            CurrentImageActivationDescriptorV2 moved = std::move(descriptor);
            const auto reused = admitCurrentImagePreRegistration(std::move(descriptor));
            if (reused || reused.error().stage != ActivationEligibilityStageV2::PreRegistration ||
                reused.error().code != ActivationEligibilityErrorCodeV2::DescriptorMovedFrom ||
                reused.error().field != ActivationEligibilityFieldV2::CurrentImageDescriptor) {
                return false;
            }

            const auto admitted = admitCurrentImagePreRegistration(std::move(moved));
            return admitted && recordingFunctionInvocationCount() == 0 &&
                   providerInvocationCount() == 0 && lifecycleInvocationCount() == 0;
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool invalidDescriptorMatrixFailsBeforeProviderCode() {
            struct Case final {
                CurrentImageDescriptorMutationV2 mutation;
                ActivationEligibilityErrorCodeV2 code;
                ActivationEligibilityFieldV2 field;
            };
            constexpr std::array cases{
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidHostIdentity,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::HostIdentity,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidEffectiveSession,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::EffectiveSessionIntegrity,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidStaticComposition,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::StaticComposition,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidBlueprintIntegrity,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::BlueprintIntegrity,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::UnsupportedTemplateRenderer,
                    .code = ActivationEligibilityErrorCodeV2::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV2::GenerationTuple,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::UnsupportedCompositionRenderer,
                    .code = ActivationEligibilityErrorCodeV2::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV2::GenerationTuple,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::UnsupportedProviderApi,
                    .code = ActivationEligibilityErrorCodeV2::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV2::GenerationTuple,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::UnsupportedSnapshotSchemaVersion,
                    .code = ActivationEligibilityErrorCodeV2::UnsupportedGenerationTuple,
                    .field = ActivationEligibilityFieldV2::GenerationTuple,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidLifecycleModel,
                    .code = ActivationEligibilityErrorCodeV2::ProcessProjectionInvalid,
                    .field = ActivationEligibilityFieldV2::ProcessProjection,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::InvalidFactoryReference,
                    .code = ActivationEligibilityErrorCodeV2::ProcessProjectionInvalid,
                    .field = ActivationEligibilityFieldV2::ProcessProjection,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::MissingFactoryRequirement,
                    .code = ActivationEligibilityErrorCodeV2::ProcessProjectionInvalid,
                    .field = ActivationEligibilityFieldV2::ProcessProjection,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::MissingCapacityFunction,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::RecordingFunction,
                },
                Case{
                    .mutation = CurrentImageDescriptorMutationV2::MissingRecordingFunction,
                    .code = ActivationEligibilityErrorCodeV2::CurrentImageDescriptorInvalid,
                    .field = ActivationEligibilityFieldV2::RecordingFunction,
                },
            };

            return std::ranges::all_of(cases, [](const Case& testCase) {
                resetEligibilityProbeCounts();
                const auto result = makePreRegistrationAdmission(testCase.mutation);
                return !result &&
                       result.error().stage == ActivationEligibilityStageV2::PreRegistration &&
                       result.error().code == testCase.code &&
                       result.error().field == testCase.field &&
                       recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                       lifecycleInvocationCount() == 0 &&
                       contributionAccessorInvocationCount() == 0;
            });
        }

        [[nodiscard]] bool wrongThreadConsumesDescriptorBeforeAdmission() {
            resetEligibilityProbeCounts();
            resetCurrentImageEpochForTest();
            auto descriptor = issueCurrentImageActivationDescriptor();
            if (!descriptor) {
                return false;
            }

            ActivationEligibilityErrorV2 observed{};
            bool rejected = false;
            std::jthread worker(
                [descriptor = std::move(*descriptor), &observed, &rejected]() mutable {
                    const auto result = admitCurrentImagePreRegistration(std::move(descriptor));
                    rejected = !result;
                    if (!result) {
                        observed = result.error();
                    }
                });
            worker.join();
            return rejected && observed.stage == ActivationEligibilityStageV2::PreRegistration &&
                   observed.code == ActivationEligibilityErrorCodeV2::WrongControlThread &&
                   observed.field == ActivationEligibilityFieldV2::ControlThread &&
                   recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

        [[nodiscard]] bool processEpochCanBeClaimedOnlyOnce() {
            resetEligibilityProbeCounts();
            resetCurrentImageEpochForTest();
            auto firstDescriptor = issueCurrentImageActivationDescriptor();
            auto secondDescriptor = issueCurrentImageActivationDescriptor();
            if (!firstDescriptor || !secondDescriptor) {
                return false;
            }

            const auto first = admitCurrentImagePreRegistration(std::move(*firstDescriptor));
            const auto second = admitCurrentImagePreRegistration(std::move(*secondDescriptor));
            return first && !second &&
                   second.error().stage == ActivationEligibilityStageV2::PreRegistration &&
                   second.error().code == ActivationEligibilityErrorCodeV2::ProcessEpochConsumed &&
                   second.error().field == ActivationEligibilityFieldV2::CurrentProcess &&
                   recordingFunctionInvocationCount() == 0 && providerInvocationCount() == 0 &&
                   lifecycleInvocationCount() == 0;
        }

    } // namespace

    std::span<const NamedEligibilityTestV2> preRegistrationEligibilityTests() noexcept {
        static constexpr std::array tests{
            NamedEligibilityTestV2{
                .name = "sealed V2 types are linear",
                .function = &sealedTypesAreLinear,
            },
            NamedEligibilityTestV2{
                .name = "valid descriptor produces one admission",
                .function = &validDescriptorProducesOneAdmissionWithoutInvokingCode,
            },
            NamedEligibilityTestV2{
                .name = "moved descriptor source fails closed",
                .function = &movedDescriptorSourceFailsClosed,
            },
            NamedEligibilityTestV2{
                .name = "invalid descriptor matrix fails before provider code",
                .function = &invalidDescriptorMatrixFailsBeforeProviderCode,
            },
            NamedEligibilityTestV2{
                .name = "wrong thread consumes descriptor",
                .function = &wrongThreadConsumesDescriptorBeforeAdmission,
            },
            NamedEligibilityTestV2{
                .name = "process epoch is claimed only once",
                .function = &processEpochCanBeClaimedOnlyOnce,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests

int main() {
    using asharia::host_runtime::tests::NamedEligibilityTestV2;
    try {
        const std::array groups{
            asharia::host_runtime::tests::preRegistrationEligibilityTests(),
            asharia::host_runtime::tests::admittedStaticFactoryRecordingTests(),
            asharia::host_runtime::tests::activationAdmissionTests(),
        };

        const bool succeeded =
            std::ranges::all_of(groups, [](const std::span<const NamedEligibilityTestV2> group) {
                return std::ranges::all_of(group, [](const NamedEligibilityTestV2& test) {
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

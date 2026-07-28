#pragma once

#include <cstddef>
#include <span>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

namespace asharia::host_runtime::tests {

    enum class CurrentImageDescriptorMutationV2 {
        None,
        InvalidHostIdentity,
        InvalidEffectiveSession,
        InvalidStaticComposition,
        InvalidBlueprintIntegrity,
        UnsupportedTemplateRenderer,
        UnsupportedCompositionRenderer,
        UnsupportedProviderApi,
        UnsupportedSnapshotSchemaVersion,
        InvalidLifecycleModel,
        InvalidFactoryReference,
        MissingFactoryRequirement,
        MissingCapacityFunction,
        MissingRecordingFunction,
        InvalidCapacityFunction,
        RegistrationFailureFunction,
        UnexpectedSnapshotFunction,
        AlternateCallbacksFunction,
        ZeroFactoryFunction,
    };

    [[nodiscard]] ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
    issueCurrentImageActivationDescriptor(CurrentImageDescriptorMutationV2 mutation =
                                              CurrentImageDescriptorMutationV2::None) noexcept;

    void resetEligibilityProbeCounts() noexcept;
    [[nodiscard]] std::size_t recordingFunctionInvocationCount() noexcept;
    [[nodiscard]] std::size_t providerInvocationCount() noexcept;
    [[nodiscard]] std::size_t lifecycleInvocationCount() noexcept;
    [[nodiscard]] std::size_t contributionAccessorInvocationCount() noexcept;

    [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEligibilityEvidenceOnlyTable(bool useAlternateCallbacks) noexcept;
    [[nodiscard]] bool eligibilityCallbackTableUsesExpected(
        std::span<const StaticFactoryCallbacksV1> callbacks) noexcept;

} // namespace asharia::host_runtime::tests

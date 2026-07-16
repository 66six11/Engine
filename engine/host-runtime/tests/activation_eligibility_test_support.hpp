#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

namespace asharia::host_runtime::tests {

    enum class EligibilityHandoffMutationV1 {
        None,
        InvalidReadySession,
        InvalidBinding,
        SessionFingerprintMismatch,
        HostIdentityMismatch,
        BlueprintMismatch,
        StaticCompositionMismatch,
        HostTemplateMismatch,
        UnsupportedTemplateRenderer,
        UnsupportedCompositionRenderer,
        UnsupportedProviderApi,
        UnsupportedSnapshotSchemaVersion,
        BindingGenerationMismatch,
        ArtifactMismatch,
        ExpectedSnapshotInvalid,
        LaunchProcessEpochMissing,
        LaunchProcessEpochStale,
        LaunchProcessEpochConsumed,
        LaunchControlThreadEpochMissing,
        LaunchRecordingFunctionMissing,
        InvalidCapacityFunction,
        RegistrationFailureFunction,
        UnexpectedSnapshotFunction,
        AlternateCallbacksFunction,
        ZeroFactoryFunction,
    };

    struct EligibilityHandoffsV1 final {
        ReadySessionHandoffV1 readySession;
        VerifiedHostActivationBlueprintHandoffV1 blueprint;
        DeepVerifiedHostBindingHandoffV1 binding;
        VerifiedCurrentProcessLaunchHandoffV1 launchHandoff;
    };

    struct NamedEligibilityTestV1 final {
        std::string_view name;
        bool (*function)();
    };

    [[nodiscard]] EligibilityHandoffsV1
    makeEligibilityHandoffs(EligibilityHandoffMutationV1 mutation =
                                EligibilityHandoffMutationV1::None);

    [[nodiscard]] ActivationEligibilityResultV1<PreRegistrationAdmissionV1>
    makePreRegistrationAdmission(
        EligibilityHandoffMutationV1 mutation = EligibilityHandoffMutationV1::None);

    void resetEligibilityProbeCounts() noexcept;
    [[nodiscard]] std::size_t recordingFunctionInvocationCount() noexcept;
    [[nodiscard]] std::size_t providerInvocationCount() noexcept;
    [[nodiscard]] std::size_t lifecycleInvocationCount() noexcept;
    [[nodiscard]] std::size_t contributionAccessorInvocationCount() noexcept;

    [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEvidenceOnlyTable(bool useAlternateCallbacks) noexcept;

    [[nodiscard]] PendingActivationFactoryTableV1
    markPendingTableEvidenceOnly(PendingActivationFactoryTableV1 pendingTable) noexcept;
    [[nodiscard]] PendingActivationFactoryTableV1
    corruptPendingTableAddress(PendingActivationFactoryTableV1 pendingTable) noexcept;
    [[nodiscard]] PendingActivationFactoryTableV1
    corruptPendingExpectedSnapshot(PendingActivationFactoryTableV1 pendingTable) noexcept;
    [[nodiscard]] std::optional<PendingActivationFactoryTableV1>
    replacePendingWithEquivalentTable(
        PendingActivationFactoryTableV1 pendingTable) noexcept;

    void rebindCurrentProcessEpochForTest();

    [[nodiscard]] std::optional<std::size_t> admittedDescriptorCount(
        const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept;
    [[nodiscard]] bool admittedTableUsesExpectedCallbacks(
        const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept;

    [[nodiscard]] std::span<const NamedEligibilityTestV1>
    preRegistrationEligibilityTests() noexcept;
    [[nodiscard]] std::span<const NamedEligibilityTestV1>
    admittedStaticFactoryRecordingTests() noexcept;
    [[nodiscard]] std::span<const NamedEligibilityTestV1>
    activationAdmissionTests() noexcept;

} // namespace asharia::host_runtime::tests

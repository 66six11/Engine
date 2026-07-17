#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include "current_image_activation_test_provider.hpp"

namespace asharia::host_runtime::tests {

    struct NamedEligibilityTestV2 final {
        std::string_view name;
        bool (*function)();
    };

    [[nodiscard]] ActivationEligibilityResultV2<PreRegistrationAdmissionV2>
    makePreRegistrationAdmission(
        CurrentImageDescriptorMutationV2 mutation = CurrentImageDescriptorMutationV2::None);

    [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEvidenceOnlyTable(bool useAlternateCallbacks) noexcept;

    [[nodiscard]] PendingActivationFactoryTableV2
    markPendingTableEvidenceOnly(PendingActivationFactoryTableV2 pendingTable) noexcept;
    [[nodiscard]] PendingActivationFactoryTableV2
    corruptPendingTableAddress(PendingActivationFactoryTableV2 pendingTable) noexcept;
    [[nodiscard]] std::optional<PendingActivationFactoryTableV2>
    replacePendingWithEquivalentTable(PendingActivationFactoryTableV2 pendingTable) noexcept;

    void resetCurrentImageEpochForTest();
    void rebindCurrentProcessEpochForTest();

    [[nodiscard]] std::optional<std::size_t>
    admittedDescriptorCount(const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept;
    [[nodiscard]] bool admittedTableUsesExpectedCallbacks(
        const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept;

    [[nodiscard]] std::span<const NamedEligibilityTestV2>
    preRegistrationEligibilityTests() noexcept;
    [[nodiscard]] std::span<const NamedEligibilityTestV2>
    admittedStaticFactoryRecordingTests() noexcept;
    [[nodiscard]] std::span<const NamedEligibilityTestV2> activationAdmissionTests() noexcept;

} // namespace asharia::host_runtime::tests

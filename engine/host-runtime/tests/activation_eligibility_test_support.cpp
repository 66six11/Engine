#include "activation_eligibility_test_support.hpp"

#include <memory>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "admitted_static_factory_callback_table_access.hpp"

namespace asharia::host_runtime::tests {

    ActivationEligibilityResultV2<PreRegistrationAdmissionV2>
    makePreRegistrationAdmission(CurrentImageDescriptorMutationV2 mutation) {
        resetCurrentImageEpochForTest();
        auto descriptor = issueCurrentImageActivationDescriptor(mutation);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        return admitCurrentImagePreRegistration(std::move(*descriptor));
    }

    StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEvidenceOnlyTable(bool useAlternateCallbacks) noexcept {
        return collectEligibilityEvidenceOnlyTable(useAlternateCallbacks);
    }

    PendingActivationFactoryTableV2
    markPendingTableEvidenceOnly(PendingActivationFactoryTableV2 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV2::take(std::move(pendingTable));
        state->origin = PendingFactoryTableOriginV2::EvidenceOnly;
        return ActivationEligibilityStateAccessV2::makePendingTable(std::move(state));
    }

    PendingActivationFactoryTableV2
    corruptPendingTableAddress(PendingActivationFactoryTableV2 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV2::take(std::move(pendingTable));
        state->expectedTableAddress = nullptr;
        return ActivationEligibilityStateAccessV2::makePendingTable(std::move(state));
    }

    std::optional<PendingActivationFactoryTableV2>
    replacePendingWithEquivalentTable(PendingActivationFactoryTableV2 pendingTable) noexcept {
        auto replacement = collectEvidenceOnlyTable(true);
        if (!replacement) {
            return std::nullopt;
        }
        auto state = ActivationEligibilityStateAccessV2::take(std::move(pendingTable));
        if (!state) {
            return std::nullopt;
        }

        std::destroy_at(std::addressof(state->table));
        std::construct_at(std::addressof(state->table), std::move(*replacement));
        return ActivationEligibilityStateAccessV2::makePendingTable(std::move(state));
    }

    void resetCurrentImageEpochForTest() {
        [[maybe_unused]] const auto processEpoch = createAndBindCurrentProcessEpoch();
        [[maybe_unused]] const auto controlThreadEpoch = createAndBindCurrentControlThreadEpoch();
    }

    void rebindCurrentProcessEpochForTest() {
        [[maybe_unused]] const auto processEpoch = createAndBindCurrentProcessEpoch();
    }

    std::optional<std::size_t>
    admittedDescriptorCount(const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept {
        const auto callbacks = AdmittedStaticFactoryCallbackTableAccessV2::callbacks(admittedTable);
        if (!callbacks) {
            return std::nullopt;
        }
        return callbacks->size();
    }

    bool admittedTableUsesExpectedCallbacks(
        const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept {
        const auto callbacks = AdmittedStaticFactoryCallbackTableAccessV2::callbacks(admittedTable);
        return callbacks && eligibilityCallbackTableUsesExpected(*callbacks);
    }

} // namespace asharia::host_runtime::tests

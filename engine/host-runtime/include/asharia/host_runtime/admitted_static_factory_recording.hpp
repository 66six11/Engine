#pragma once

#include <memory>

#include "asharia/host_runtime/activation_eligibility.hpp"
#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime {

    struct PendingActivationFactoryTableStateV2;
    struct AdmittedStaticFactoryCallbackTableStateV2;
    class ActivationEligibilityStateAccessV2;
    class AdmittedStaticFactoryCallbackTableAccessV2;

    using StaticFactoryRegistrationCapacityFunctionV2 =
        StaticFactoryRegistrationCapacityV2 (*)() noexcept;
    using StaticFactoryRecordingFunctionV2 =
        void (*)(StaticFactoryRegistrationRecorder& recorder) noexcept;

    class PendingActivationFactoryTableV2 final {
    public:
        ~PendingActivationFactoryTableV2();

        PendingActivationFactoryTableV2(PendingActivationFactoryTableV2&&) noexcept;
        PendingActivationFactoryTableV2& operator=(PendingActivationFactoryTableV2&&) = delete;
        PendingActivationFactoryTableV2(const PendingActivationFactoryTableV2&) = delete;
        PendingActivationFactoryTableV2& operator=(const PendingActivationFactoryTableV2&) = delete;

    private:
        explicit PendingActivationFactoryTableV2(
            std::unique_ptr<PendingActivationFactoryTableStateV2> state) noexcept;

        std::unique_ptr<PendingActivationFactoryTableStateV2> state_;

        friend class ActivationEligibilityStateAccessV2;
    };

    class ActivationAdmissionV2 final {
    public:
        ~ActivationAdmissionV2() = default;

        ActivationAdmissionV2(ActivationAdmissionV2&& other) noexcept;
        ActivationAdmissionV2& operator=(ActivationAdmissionV2&&) = delete;
        ActivationAdmissionV2(const ActivationAdmissionV2&) = delete;
        ActivationAdmissionV2& operator=(const ActivationAdmissionV2&) = delete;

    private:
        explicit ActivationAdmissionV2(bool valid) noexcept : valid_(valid) {}

        bool valid_{};

        friend class ActivationEligibilityStateAccessV2;
        friend class AdmittedStaticFactoryCallbackTableAccessV2;
        friend class AdmittedStaticFactoryCallbackTableV2;
    };

    class AdmittedStaticFactoryCallbackTableV2 final {
    public:
        ~AdmittedStaticFactoryCallbackTableV2();

        AdmittedStaticFactoryCallbackTableV2(AdmittedStaticFactoryCallbackTableV2&& other) noexcept;
        AdmittedStaticFactoryCallbackTableV2&
        operator=(AdmittedStaticFactoryCallbackTableV2&&) = delete;
        AdmittedStaticFactoryCallbackTableV2(const AdmittedStaticFactoryCallbackTableV2&) = delete;
        AdmittedStaticFactoryCallbackTableV2&
        operator=(const AdmittedStaticFactoryCallbackTableV2&) = delete;

        [[nodiscard]] const StaticFactoryRegistrationSnapshotV2&
        registrationSnapshot() const noexcept;

    private:
        AdmittedStaticFactoryCallbackTableV2(
            std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV2> state,
            ActivationAdmissionV2 admission) noexcept;

        std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV2> state_;
        ActivationAdmissionV2 admission_;

        friend class ActivationEligibilityStateAccessV2;
        friend class AdmittedStaticFactoryCallbackTableAccessV2;
    };

    [[nodiscard]] ActivationEligibilityResultV2<PendingActivationFactoryTableV2>
    recordAdmittedStaticFactoryProviders(PreRegistrationAdmissionV2 admission) noexcept;

    [[nodiscard]] ActivationEligibilityResultV2<AdmittedStaticFactoryCallbackTableV2>
    admitStaticFactoryActivation(PendingActivationFactoryTableV2 pendingTable) noexcept;

} // namespace asharia::host_runtime

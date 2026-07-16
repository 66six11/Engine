#pragma once

#include <memory>
#include <optional>
#include <span>

#include "asharia/host_runtime/activation_eligibility.hpp"
#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime {

    struct PendingActivationFactoryTableStateV1;
    struct AdmittedStaticFactoryCallbackTableStateV1;
    class ActivationEligibilityStateAccessV1;
    class AdmittedStaticFactoryCallbackTableAccessV1;

    using StaticFactoryRegistrationCapacityFunctionV1 =
        StaticFactoryRegistrationCapacityV1 (*)() noexcept;
    using StaticFactoryRecordingFunctionV1 =
        void (*)(StaticFactoryRegistrationRecorder& recorder) noexcept;

    class PendingActivationFactoryTableV1 final {
    public:
        ~PendingActivationFactoryTableV1();

        PendingActivationFactoryTableV1(PendingActivationFactoryTableV1&&) noexcept;
        PendingActivationFactoryTableV1&
        operator=(PendingActivationFactoryTableV1&&) = delete;
        PendingActivationFactoryTableV1(const PendingActivationFactoryTableV1&) = delete;
        PendingActivationFactoryTableV1&
        operator=(const PendingActivationFactoryTableV1&) = delete;

    private:
        explicit PendingActivationFactoryTableV1(
            std::unique_ptr<PendingActivationFactoryTableStateV1> state) noexcept;

        std::unique_ptr<PendingActivationFactoryTableStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    class ActivationAdmissionV1 final {
    public:
        ~ActivationAdmissionV1() = default;

        ActivationAdmissionV1(ActivationAdmissionV1&& other) noexcept;
        ActivationAdmissionV1& operator=(ActivationAdmissionV1&&) = delete;
        ActivationAdmissionV1(const ActivationAdmissionV1&) = delete;
        ActivationAdmissionV1& operator=(const ActivationAdmissionV1&) = delete;

    private:
        explicit ActivationAdmissionV1(bool valid) noexcept : valid_(valid) {}

        bool valid_{};

        friend class ActivationEligibilityStateAccessV1;
        friend class AdmittedStaticFactoryCallbackTableV1;
    };

    class AdmittedStaticFactoryCallbackTableV1 final {
    public:
        ~AdmittedStaticFactoryCallbackTableV1();

        AdmittedStaticFactoryCallbackTableV1(
            AdmittedStaticFactoryCallbackTableV1&& other) noexcept;
        AdmittedStaticFactoryCallbackTableV1&
        operator=(AdmittedStaticFactoryCallbackTableV1&&) = delete;
        AdmittedStaticFactoryCallbackTableV1(
            const AdmittedStaticFactoryCallbackTableV1&) = delete;
        AdmittedStaticFactoryCallbackTableV1&
        operator=(const AdmittedStaticFactoryCallbackTableV1&) = delete;

        [[nodiscard]] const StaticFactoryRegistrationSnapshotV1&
        registrationSnapshot() const noexcept;

    private:
        AdmittedStaticFactoryCallbackTableV1(
            std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV1> state,
            ActivationAdmissionV1 admission) noexcept;

        std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV1> state_;
        ActivationAdmissionV1 admission_;

        [[nodiscard]] std::optional<std::span<const StaticFactoryCallbacksV1>>
        callbackDescriptorsForHostRuntime() const noexcept;

        friend class ActivationEligibilityStateAccessV1;
        friend class AdmittedStaticFactoryCallbackTableAccessV1;
    };

    [[nodiscard]] ActivationEligibilityResultV1<PendingActivationFactoryTableV1>
    recordAdmittedStaticFactoryProviders(PreRegistrationAdmissionV1 admission) noexcept;

    [[nodiscard]] ActivationEligibilityResultV1<AdmittedStaticFactoryCallbackTableV1>
    admitStaticFactoryActivation(PendingActivationFactoryTableV1 pendingTable) noexcept;

} // namespace asharia::host_runtime

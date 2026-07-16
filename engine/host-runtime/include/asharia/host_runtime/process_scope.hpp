#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"
#include "asharia/host_runtime/factory_lifecycle_contexts.hpp"
#include "asharia/host_runtime/process_contribution_registry.hpp"

namespace asharia::host_runtime {

    enum class ProcessScopeStateV2 : std::uint8_t {
        Prepared,
        Active,
        StartFailed,
        Stopped,
        MovedFrom,
    };

    enum class ProcessScopeLifecycleStageV2 : std::uint8_t {
        Create,
        Activate,
        Quiesce,
        Deactivate,
    };

    enum class ProcessScopeContributionPublicationStageV2 : std::uint8_t {
        PayloadAccess,
        LeaseCommit,
    };

    enum class ProcessScopeErrorCodeV2 : std::uint8_t {
        AdmissionMovedFrom,
        WrongControlThread,
        ProcessEpochStale,
        AdmittedTableInvalid,
        ProcessProjectionInvalid,
        ProcessScopeExpected,
        ParentScopeInvalid,
        EngineGenerationMismatch,
        BlueprintMismatch,
        LifecycleModelMismatch,
        DescriptorCountMismatch,
        DescriptorDuplicate,
        DescriptorMissing,
        CallbackMissing,
        FactoryDuplicate,
        RequirementDuplicate,
        RequirementMissing,
        RequirementOrderInvalid,
        ContributionRuntimeBindingInvalid,
        ContributionSingleConflict,
        AllocationFailed,
        ExecutorMovedFrom,
        OperationInProgress,
        StartRequiresPrepared,
        StopRequiresActive,
        FactoryCreateFailed,
        FactoryActivateFailed,
        ContributionAccessorReturnedNull,
        ContributionPublicationFailed,
    };

    struct ExactFactoryReferenceV2 final {
        std::string packageId;
        std::string packageVersion;
        std::string moduleId;
        std::string factoryId;

        [[nodiscard]] ExactFactoryReferenceViewV1 view() const noexcept {
            return {
                .packageId = packageId,
                .packageVersion = packageVersion,
                .moduleId = moduleId,
                .factoryId = factoryId,
            };
        }

        [[nodiscard]] friend bool operator==(const ExactFactoryReferenceV2&,
                                             const ExactFactoryReferenceV2&) = default;
    };

    struct ProcessScopePreparationErrorV2 final {
        ProcessScopeErrorCodeV2 code{ProcessScopeErrorCodeV2::AdmissionMovedFrom};
        std::optional<ExactFactoryReferenceV2> factory;
        std::optional<ExactFactoryReferenceV2> requirement;
        std::optional<std::string> contributionId;
        std::optional<std::string> contributionKind;
    };

    struct ProcessScopeOperationErrorV2 final {
        ProcessScopeErrorCodeV2 code{ProcessScopeErrorCodeV2::ExecutorMovedFrom};
        ProcessScopeStateV2 state{ProcessScopeStateV2::MovedFrom};
    };

    struct ProcessScopeContributionDiagnosticAttributionStateV2;
    struct ProcessScopeDiagnosticAttributionStateV2;
    class ProcessScopeStateAccessV2;

    class ProcessScopeLifecycleDiagnosticV2 final {
    public:
        ProcessScopeLifecycleDiagnosticV2(const ProcessScopeLifecycleDiagnosticV2&) noexcept =
            default;
        ProcessScopeLifecycleDiagnosticV2&
        operator=(const ProcessScopeLifecycleDiagnosticV2&) noexcept = default;
        ProcessScopeLifecycleDiagnosticV2(ProcessScopeLifecycleDiagnosticV2&&) noexcept = default;
        ProcessScopeLifecycleDiagnosticV2&
        operator=(ProcessScopeLifecycleDiagnosticV2&&) noexcept = default;
        ~ProcessScopeLifecycleDiagnosticV2() = default;

        [[nodiscard]] ProcessScopeLifecycleStageV2 stage() const noexcept;
        // Returned views remain valid only while this diagnostic or one of its copies
        // keeps the shared owning attribution alive.
        [[nodiscard]] std::string_view engineGenerationId() const noexcept;
        [[nodiscard]] ExactFactoryReferenceViewV1 factory() const noexcept;
        [[nodiscard]] std::uint32_t providerLocalCode() const noexcept;

    private:
        ProcessScopeLifecycleDiagnosticV2(
            std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV2> attribution,
            ProcessScopeLifecycleStageV2 stage, std::uint32_t providerLocalCode) noexcept;

        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV2> attribution_;
        ProcessScopeLifecycleStageV2 stage_{ProcessScopeLifecycleStageV2::Create};
        std::uint32_t providerLocalCode_{};

        friend class ProcessScopeStateAccessV2;
    };

    class ProcessScopeContributionPublicationDiagnosticV2 final {
    public:
        ProcessScopeContributionPublicationDiagnosticV2(
            const ProcessScopeContributionPublicationDiagnosticV2&) noexcept = default;
        ProcessScopeContributionPublicationDiagnosticV2&
        operator=(const ProcessScopeContributionPublicationDiagnosticV2&) noexcept = default;
        ProcessScopeContributionPublicationDiagnosticV2(
            ProcessScopeContributionPublicationDiagnosticV2&&) noexcept = default;
        ProcessScopeContributionPublicationDiagnosticV2&
        operator=(ProcessScopeContributionPublicationDiagnosticV2&&) noexcept = default;
        ~ProcessScopeContributionPublicationDiagnosticV2() = default;

        [[nodiscard]] ProcessScopeContributionPublicationStageV2 stage() const noexcept;
        [[nodiscard]] std::string_view engineGenerationId() const noexcept;
        [[nodiscard]] ExactFactoryReferenceViewV1 factory() const noexcept;
        [[nodiscard]] std::string_view contributionId() const noexcept;
        [[nodiscard]] std::string_view contributionKind() const noexcept;

    private:
        ProcessScopeContributionPublicationDiagnosticV2(
            std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2> attribution,
            ProcessScopeContributionPublicationStageV2 stage) noexcept;

        std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2> attribution_;
        ProcessScopeContributionPublicationStageV2 stage_{
            ProcessScopeContributionPublicationStageV2::PayloadAccess};

        friend class ProcessScopeStateAccessV2;
    };

    struct ProcessScopeStartFailureV2 final {
        ProcessScopeOperationErrorV2 operation;
        std::optional<ProcessScopeLifecycleDiagnosticV2> primary;
        std::optional<ProcessScopeContributionPublicationDiagnosticV2> publication;
        std::vector<ProcessScopeLifecycleDiagnosticV2> cleanupDiagnostics;
    };

    struct ProcessScopeStopReportV2 final {
        std::vector<ProcessScopeLifecycleDiagnosticV2> cleanupDiagnostics;

        [[nodiscard]] bool callbacksSucceeded() const noexcept {
            return cleanupDiagnostics.empty();
        }
    };

    using ProcessScopeStartResultV2 = std::expected<void, ProcessScopeStartFailureV2>;
    using ProcessScopeStopResultV2 =
        std::expected<ProcessScopeStopReportV2, ProcessScopeOperationErrorV2>;

    struct ProcessScopeExecutorStateV2;

    class ProcessScopeExecutorV2 final {
    public:
        ~ProcessScopeExecutorV2() noexcept;

        ProcessScopeExecutorV2(ProcessScopeExecutorV2&&) noexcept;
        ProcessScopeExecutorV2& operator=(ProcessScopeExecutorV2&&) = delete;
        ProcessScopeExecutorV2(const ProcessScopeExecutorV2&) = delete;
        ProcessScopeExecutorV2& operator=(const ProcessScopeExecutorV2&) = delete;

        [[nodiscard]] ProcessScopeStateV2 state() const noexcept;
        [[nodiscard]] std::expected<ProcessContributionRegistryViewV1,
                                    ProcessContributionLookupErrorV1>
        contributions() const noexcept;
        [[nodiscard]] ProcessScopeStartResultV2 start() noexcept;
        [[nodiscard]] ProcessScopeStopResultV2 stop() noexcept;

    private:
        explicit ProcessScopeExecutorV2(
            std::unique_ptr<ProcessScopeExecutorStateV2> state) noexcept;

        std::unique_ptr<ProcessScopeExecutorStateV2> state_;

        friend class ProcessScopeStateAccessV2;
    };

    using ProcessScopePreparationResultV2 =
        std::expected<ProcessScopeExecutorV2, ProcessScopePreparationErrorV2>;

    [[nodiscard]] ProcessScopePreparationResultV2
    prepareProcessScopeExecutorV2(AdmittedStaticFactoryCallbackTableV1 admittedTable) noexcept;

} // namespace asharia::host_runtime

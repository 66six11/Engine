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

namespace asharia::host_runtime {

    enum class ProcessScopeStateV1 : std::uint8_t {
        Prepared,
        Active,
        StartFailed,
        Stopped,
        MovedFrom,
    };

    enum class ProcessScopeLifecycleStageV1 : std::uint8_t {
        Create,
        Activate,
        Quiesce,
        Deactivate,
    };

    enum class ProcessScopeErrorCodeV1 : std::uint8_t {
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
        AllocationFailed,
        ExecutorMovedFrom,
        OperationInProgress,
        StartRequiresPrepared,
        StopRequiresActive,
        FactoryCreateFailed,
        FactoryActivateFailed,
    };

    struct ExactFactoryReferenceV1 final {
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

        [[nodiscard]] friend bool operator==(const ExactFactoryReferenceV1&,
                                             const ExactFactoryReferenceV1&) = default;
    };

    struct ProcessScopePreparationErrorV1 final {
        ProcessScopeErrorCodeV1 code{ProcessScopeErrorCodeV1::AdmissionMovedFrom};
        std::optional<ExactFactoryReferenceV1> factory;
        std::optional<ExactFactoryReferenceV1> requirement;
    };

    struct ProcessScopeOperationErrorV1 final {
        ProcessScopeErrorCodeV1 code{ProcessScopeErrorCodeV1::ExecutorMovedFrom};
        ProcessScopeStateV1 state{ProcessScopeStateV1::MovedFrom};
    };

    struct ProcessScopeDiagnosticAttributionStateV1;
    class ProcessScopeStateAccessV1;

    class ProcessScopeLifecycleDiagnosticV1 final {
    public:
        ProcessScopeLifecycleDiagnosticV1(const ProcessScopeLifecycleDiagnosticV1&) noexcept =
            default;
        ProcessScopeLifecycleDiagnosticV1&
        operator=(const ProcessScopeLifecycleDiagnosticV1&) noexcept = default;
        ProcessScopeLifecycleDiagnosticV1(ProcessScopeLifecycleDiagnosticV1&&) noexcept = default;
        ProcessScopeLifecycleDiagnosticV1&
        operator=(ProcessScopeLifecycleDiagnosticV1&&) noexcept = default;
        ~ProcessScopeLifecycleDiagnosticV1() = default;

        [[nodiscard]] ProcessScopeLifecycleStageV1 stage() const noexcept;
        // Returned views remain valid only while this diagnostic or one of its copies
        // keeps the shared owning attribution alive.
        [[nodiscard]] std::string_view engineGenerationId() const noexcept;
        [[nodiscard]] ExactFactoryReferenceViewV1 factory() const noexcept;
        [[nodiscard]] std::uint32_t providerLocalCode() const noexcept;

    private:
        ProcessScopeLifecycleDiagnosticV1(
            std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV1> attribution,
            ProcessScopeLifecycleStageV1 stage, std::uint32_t providerLocalCode) noexcept;

        std::shared_ptr<const ProcessScopeDiagnosticAttributionStateV1> attribution_;
        ProcessScopeLifecycleStageV1 stage_{ProcessScopeLifecycleStageV1::Create};
        std::uint32_t providerLocalCode_{};

        friend class ProcessScopeStateAccessV1;
    };

    struct ProcessScopeStartFailureV1 final {
        ProcessScopeOperationErrorV1 operation;
        std::optional<ProcessScopeLifecycleDiagnosticV1> primary;
        std::vector<ProcessScopeLifecycleDiagnosticV1> cleanupDiagnostics;
    };

    struct ProcessScopeStopReportV1 final {
        std::vector<ProcessScopeLifecycleDiagnosticV1> cleanupDiagnostics;

        [[nodiscard]] bool callbacksSucceeded() const noexcept {
            return cleanupDiagnostics.empty();
        }
    };

    using ProcessScopeStartResultV1 = std::expected<void, ProcessScopeStartFailureV1>;
    using ProcessScopeStopResultV1 =
        std::expected<ProcessScopeStopReportV1, ProcessScopeOperationErrorV1>;

    struct ProcessScopeExecutorStateV1;

    class ProcessScopeExecutorV1 final {
    public:
        ~ProcessScopeExecutorV1() noexcept;

        ProcessScopeExecutorV1(ProcessScopeExecutorV1&&) noexcept;
        ProcessScopeExecutorV1& operator=(ProcessScopeExecutorV1&&) = delete;
        ProcessScopeExecutorV1(const ProcessScopeExecutorV1&) = delete;
        ProcessScopeExecutorV1& operator=(const ProcessScopeExecutorV1&) = delete;

        [[nodiscard]] ProcessScopeStateV1 state() const noexcept;
        [[nodiscard]] ProcessScopeStartResultV1 start() noexcept;
        [[nodiscard]] ProcessScopeStopResultV1 stop() noexcept;

    private:
        explicit ProcessScopeExecutorV1(
            std::unique_ptr<ProcessScopeExecutorStateV1> state) noexcept;

        std::unique_ptr<ProcessScopeExecutorStateV1> state_;

        friend class ProcessScopeStateAccessV1;
    };

    using ProcessScopePreparationResultV1 =
        std::expected<ProcessScopeExecutorV1, ProcessScopePreparationErrorV1>;

    [[nodiscard]] ProcessScopePreparationResultV1
    prepareProcessScopeExecutor(AdmittedStaticFactoryCallbackTableV1 admittedTable) noexcept;

} // namespace asharia::host_runtime

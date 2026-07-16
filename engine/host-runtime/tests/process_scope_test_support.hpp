#pragma once

#include <span>
#include <string_view>

#include "asharia/host_runtime/process_scope.hpp"

namespace asharia::host_runtime::tests {

    enum class ProcessPlanMutationV1 : std::uint8_t {
        None,
        Empty,
        NonProcessScope,
        ParentedProcessScope,
        EngineGenerationMismatch,
        BlueprintMismatch,
        LifecycleModelMismatch,
        PackageIdentityMismatch,
        VersionIdentityMismatch,
        ModuleIdentityMismatch,
        FactoryIdentityMismatch,
        DuplicateFactory,
        DuplicateRequirement,
        MissingRequirement,
        ForwardRequirement,
    };

    struct NamedProcessScopeTestV1 final {
        std::string_view name;
        bool (*function)();
    };

    [[nodiscard]] ActivationEligibilityResultV1<AdmittedStaticFactoryCallbackTableV1>
    makeAdmittedSyntheticProcessScope(ProcessPlanMutationV1 mutation = ProcessPlanMutationV1::None);

    void rebindSyntheticCurrentProcessEpoch();

    [[nodiscard]] std::span<const NamedProcessScopeTestV1> processScopeContractTests() noexcept;
    [[nodiscard]] std::span<const NamedProcessScopeTestV1> processScopePreflightTests() noexcept;
    [[nodiscard]] std::span<const NamedProcessScopeTestV1> processScopeStartupTests() noexcept;
    [[nodiscard]] std::span<const NamedProcessScopeTestV1> processScopeCleanupTests() noexcept;

    [[noreturn]] void runActiveProcessScopeDropProbe();

} // namespace asharia::host_runtime::tests

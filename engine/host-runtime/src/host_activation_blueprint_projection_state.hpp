#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace asharia::host_runtime {

    enum class HostScopeKindStateV1 : std::uint8_t {
        Process,
        Project,
        Editor,
        ToolJob,
        GameSession,
        World,
        LocalUser,
        EditorDocument,
        Preview,
    };

    struct ExactFactoryReferenceStateV1 final {
        std::string packageId;
        std::string packageVersion;
        std::string moduleId;
        std::string factoryId;

        [[nodiscard]] friend bool operator==(const ExactFactoryReferenceStateV1&,
                                             const ExactFactoryReferenceStateV1&) = default;
    };

    struct ProcessFactoryProjectionStateV1 final {
        ExactFactoryReferenceStateV1 reference;
        std::vector<ExactFactoryReferenceStateV1> requirements;
    };

    struct ProcessScopeBlueprintProjectionStateV1 final {
        HostScopeKindStateV1 scope{HostScopeKindStateV1::Process};
        std::optional<HostScopeKindStateV1> parentScope;
        std::string engineGenerationId;
        std::string blueprintIntegrity;
        std::string lifecycleModel;
        std::vector<ProcessFactoryProjectionStateV1> factories;
    };

} // namespace asharia::host_runtime

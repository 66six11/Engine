#pragma once

#include <string>
#include <vector>

namespace asharia::host_runtime {

    struct StaticFactoryRegistrationV1 final {
        std::string packageId;
        std::string packageVersion;
        std::string moduleId;
        std::string factoryId;
        std::string providerEntryPoint;

        [[nodiscard]] friend bool operator==(const StaticFactoryRegistrationV1&,
                                             const StaticFactoryRegistrationV1&) = default;
    };

    struct StaticFactoryRegistrationSnapshotV1 final {
        std::string generationId;
        std::string hostActivationBlueprintSha256;
        std::vector<StaticFactoryRegistrationV1> registrations;

        [[nodiscard]] friend bool operator==(const StaticFactoryRegistrationSnapshotV1&,
                                             const StaticFactoryRegistrationSnapshotV1&) = default;
    };

} // namespace asharia::host_runtime

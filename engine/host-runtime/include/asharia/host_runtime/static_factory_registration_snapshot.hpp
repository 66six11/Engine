#pragma once

#include <string>
#include <vector>

#include "asharia/host_runtime/static_contribution_contract.hpp"

namespace asharia::host_runtime {

    struct StaticContributionRegistrationV2 final {
        std::string contributionId;
        std::string contributionKind;
        StaticContributionCardinalityV1 cardinality{};

        [[nodiscard]] friend bool operator==(const StaticContributionRegistrationV2&,
                                             const StaticContributionRegistrationV2&) = default;
    };

    struct StaticFactoryRegistrationV2 final {
        std::string packageId;
        std::string packageVersion;
        std::string moduleId;
        std::string factoryId;
        std::string providerEntryPoint;
        std::vector<StaticContributionRegistrationV2> contributions;

        [[nodiscard]] friend bool operator==(const StaticFactoryRegistrationV2&,
                                             const StaticFactoryRegistrationV2&) = default;
    };

    struct StaticFactoryRegistrationSnapshotV2 final {
        std::string generationId;
        std::string hostActivationBlueprintSha256;
        std::vector<StaticFactoryRegistrationV2> registrations;

        [[nodiscard]] friend bool operator==(const StaticFactoryRegistrationSnapshotV2&,
                                             const StaticFactoryRegistrationSnapshotV2&) = default;
    };

} // namespace asharia::host_runtime

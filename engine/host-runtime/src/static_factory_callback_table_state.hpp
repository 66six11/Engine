#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "asharia/host_runtime/static_factory_callbacks.hpp"
#include "asharia/host_runtime/static_factory_registration_snapshot.hpp"

namespace asharia::host_runtime {

    struct StaticContributionTypeEvidenceV1 final {
        std::size_t registrationIndex{};
        std::size_t contributionIndex{};
        const void* typeKey{};
    };

    struct StaticFactoryCallbackTableStorageV1 final {
        StaticFactoryCallbackTableStorageV1(
            StaticFactoryRegistrationSnapshotV2 snapshotValue,
            std::vector<StaticFactoryCallbacksV1> callbacksValue,
            std::vector<StaticContributionTypeEvidenceV1> contributionTypeEvidenceValue) noexcept
            : snapshot(std::move(snapshotValue)), callbacks(std::move(callbacksValue)),
              contributionTypeEvidence(std::move(contributionTypeEvidenceValue)) {}

        StaticFactoryRegistrationSnapshotV2 snapshot;
        std::vector<StaticFactoryCallbacksV1> callbacks;
        std::vector<StaticContributionTypeEvidenceV1> contributionTypeEvidence;
    };

} // namespace asharia::host_runtime

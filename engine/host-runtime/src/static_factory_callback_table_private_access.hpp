#pragma once

#include <span>

#include "asharia/host_runtime/static_factory_callback_table.hpp"

#include "static_factory_callback_table_state.hpp"

namespace asharia::host_runtime {

    class StaticFactoryCallbackTablePrivateAccessV1 final {
    public:
        [[nodiscard]] static std::shared_ptr<const StaticFactoryCallbackTableStorageV1>
        instanceAnchor(const StaticFactoryCallbackTableV1& table) noexcept {
            return table.storage_;
        }

        [[nodiscard]] static std::span<const StaticFactoryCallbacksV1>
        callbacks(const StaticFactoryCallbackTableV1& table) noexcept {
            if (!table.storage_) {
                return {};
            }
            return table.storage_->callbacks;
        }

        [[nodiscard]] static std::span<const StaticContributionTypeEvidenceV1>
        contributionTypeEvidence(const StaticFactoryCallbackTableV1& table) noexcept {
            if (!table.storage_) {
                return {};
            }
            return table.storage_->contributionTypeEvidence;
        }
    };

} // namespace asharia::host_runtime

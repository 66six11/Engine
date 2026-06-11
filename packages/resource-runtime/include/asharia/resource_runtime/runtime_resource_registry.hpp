#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "asharia/asset_core/asset_handle.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/core/result.hpp"

namespace asharia::resource {

    enum class RuntimeResourceState : std::uint8_t {
        Pending,
        Ready,
        Failed,
    };

    enum class RuntimeResourceDiagnosticCode : int {
        InvalidResourceKey = 1,
        InvalidProductKey = 2,
        MissingResource = 3,
        GenerationMismatch = 4,
        ProductKeyMismatch = 5,
        ResourceNotPending = 6,
        InvalidFailure = 7,
    };

    enum class RuntimeResourceFailureReason : std::uint8_t {
        MissingProduct,
        StaleProduct,
        InvalidProductRecord,
        ProductReadFailed,
        UnsupportedProduct,
        RuntimeCreationFailed,
    };

    struct RuntimeResourceKey {
        asset::AssetGuid guid{};
        asset::AssetTypeId assetType{};

        [[nodiscard]] friend bool operator==(RuntimeResourceKey, RuntimeResourceKey) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid) && static_cast<bool>(assetType);
        }
    };

    template <class T>
    [[nodiscard]] RuntimeResourceKey makeRuntimeResourceKey(asset::AssetHandle<T> handle,
                                                            asset::AssetTypeId assetType) noexcept {
        return RuntimeResourceKey{.guid = handle.guid, .assetType = assetType};
    }

    struct RuntimeResourceTicket {
        RuntimeResourceKey key{};
        std::uint64_t generation{};

        [[nodiscard]] friend bool operator==(RuntimeResourceTicket,
                                             RuntimeResourceTicket) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(key) && generation != 0;
        }
    };

    struct RuntimeResourceFailure {
        RuntimeResourceFailureReason reason{RuntimeResourceFailureReason::MissingProduct};
        std::string message;

        [[nodiscard]] friend bool operator==(const RuntimeResourceFailure&,
                                             const RuntimeResourceFailure&) = default;
    };

    struct RuntimeResourceDiagnostic {
        RuntimeResourceDiagnosticCode code{RuntimeResourceDiagnosticCode::InvalidResourceKey};
        RuntimeResourceKey key{};
        asset::AssetProductKey expectedProductKey{};
        asset::AssetProductKey actualProductKey{};
        std::uint64_t expectedGeneration{};
        std::uint64_t actualGeneration{};
        std::string message;

        [[nodiscard]] friend bool operator==(const RuntimeResourceDiagnostic&,
                                             const RuntimeResourceDiagnostic&) = default;
    };

    struct RuntimeResourceRecord {
        RuntimeResourceKey key{};
        RuntimeResourceState state{RuntimeResourceState::Pending};
        std::uint64_t generation{};
        asset::AssetProductKey expectedProductKey{};
        asset::AssetProductRecord product{};
        RuntimeResourceFailure failure{};

        [[nodiscard]] friend bool operator==(const RuntimeResourceRecord&,
                                             const RuntimeResourceRecord&) = default;
    };

    class RuntimeResourceRegistry {
    public:
        [[nodiscard]] Result<RuntimeResourceTicket>
        request(RuntimeResourceKey key, asset::AssetProductKey expectedProductKey);

        [[nodiscard]] Result<RuntimeResourceRecord> markReady(RuntimeResourceTicket ticket,
                                                              asset::AssetProductRecord product);

        [[nodiscard]] Result<RuntimeResourceRecord> markFailed(RuntimeResourceTicket ticket,
                                                               RuntimeResourceFailure failure);

        [[nodiscard]] Result<RuntimeResourceRecord>
        resolveProductRecords(RuntimeResourceTicket ticket,
                              std::span<const asset::AssetProductRecord> products);

        [[nodiscard]] const RuntimeResourceRecord* find(RuntimeResourceKey key) const noexcept;

        [[nodiscard]] std::span<const RuntimeResourceRecord> records() const noexcept;

    private:
        std::vector<RuntimeResourceRecord> records_;
        std::uint64_t nextGeneration_{1};
    };

    [[nodiscard]] const char* runtimeResourceStateName(RuntimeResourceState state) noexcept;
    [[nodiscard]] const char*
    runtimeResourceDiagnosticCodeName(RuntimeResourceDiagnosticCode code) noexcept;
    [[nodiscard]] const char*
    runtimeResourceFailureReasonName(RuntimeResourceFailureReason reason) noexcept;

} // namespace asharia::resource

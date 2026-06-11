#include "asharia/resource_runtime/runtime_resource_registry.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <utility>

namespace asharia::resource {
    namespace {

        [[nodiscard]] Error runtimeResourceError(RuntimeResourceDiagnosticCode code,
                                                 std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] std::string keyLabel(RuntimeResourceKey key) {
            return "guid=\"" + asset::formatAssetGuid(key.guid) +
                   "\" assetType=" + std::to_string(key.assetType.value);
        }

        [[nodiscard]] std::string productKeyLabel(const asset::AssetProductKey& key) {
            return "productKey{guid=\"" + asset::formatAssetGuid(key.guid) +
                   "\" assetType=" + std::to_string(key.assetType.value) +
                   " importer=" + std::to_string(key.importerId.value) +
                   " importerVersion=" + std::to_string(key.importerVersion.value) +
                   " sourceHash=" + std::to_string(key.sourceHash) +
                   " settingsHash=" + std::to_string(key.settingsHash) +
                   " dependencyHash=" + std::to_string(key.dependencyHash) +
                   " targetProfileHash=" + std::to_string(key.targetProfileHash) + "}";
        }

        [[nodiscard]] bool productKeyMatches(RuntimeResourceKey key,
                                             const asset::AssetProductKey& productKey) noexcept {
            return productKey.guid == key.guid && productKey.assetType == key.assetType;
        }

        [[nodiscard]] bool
        productKeyHasResourceGuid(RuntimeResourceKey key,
                                  const asset::AssetProductKey& productKey) noexcept {
            return productKey.guid == key.guid;
        }

        [[nodiscard]] std::string failedResolutionMessage(RuntimeResourceKey key,
                                                          std::string reason) {
            return "Runtime resource product resolution for " + keyLabel(key) + " " +
                   std::move(reason) + ".";
        }

    } // namespace

    Result<RuntimeResourceTicket>
    RuntimeResourceRegistry::request(RuntimeResourceKey key,
                                     asset::AssetProductKey expectedProductKey) {
        if (!key) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidResourceKey,
                                     "Runtime resource request rejected invalid resource key.")};
        }

        if (!expectedProductKey) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidProductKey,
                                     "Runtime resource request for " + keyLabel(key) +
                                         " rejected invalid expected product key.")};
        }

        if (!productKeyMatches(key, expectedProductKey)) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::ProductKeyMismatch,
                "Runtime resource request for " + keyLabel(key) + " rejected mismatched " +
                    productKeyLabel(expectedProductKey) + ".")};
        }

        const RuntimeResourceTicket ticket{.key = key, .generation = nextGeneration_++};
        auto existing = std::ranges::find_if(
            records_, [key](const RuntimeResourceRecord& record) { return record.key == key; });

        RuntimeResourceRecord record{.key = key,
                                     .state = RuntimeResourceState::Pending,
                                     .generation = ticket.generation,
                                     .expectedProductKey = expectedProductKey};

        if (existing == records_.end()) {
            records_.push_back(std::move(record));
        } else {
            *existing = std::move(record);
        }

        return ticket;
    }

    Result<RuntimeResourceRecord>
    RuntimeResourceRegistry::markReady(RuntimeResourceTicket ticket,
                                       asset::AssetProductRecord product) {
        if (!ticket) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidResourceKey,
                                     "Runtime resource ready update rejected invalid ticket.")};
        }

        if (!product) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidProductKey,
                                     "Runtime resource ready update for " + keyLabel(ticket.key) +
                                         " rejected invalid product record.")};
        }

        auto existing =
            std::ranges::find_if(records_, [key = ticket.key](const RuntimeResourceRecord& record) {
                return record.key == key;
            });
        if (existing == records_.end()) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::MissingResource,
                "Runtime resource ready update could not find " + keyLabel(ticket.key) + ".")};
        }

        if (existing->generation != ticket.generation) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::GenerationMismatch,
                "Runtime resource ready update for " + keyLabel(ticket.key) +
                    " rejected stale generation expected=" + std::to_string(existing->generation) +
                    " actual=" + std::to_string(ticket.generation) + ".")};
        }

        if (existing->state != RuntimeResourceState::Pending) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::ResourceNotPending,
                "Runtime resource ready update for " + keyLabel(ticket.key) + " rejected state=\"" +
                    runtimeResourceStateName(existing->state) + "\".")};
        }

        if (product.key != existing->expectedProductKey) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::ProductKeyMismatch,
                "Runtime resource ready update for " + keyLabel(ticket.key) + " rejected " +
                    productKeyLabel(product.key) + " expected " +
                    productKeyLabel(existing->expectedProductKey) + ".")};
        }

        existing->state = RuntimeResourceState::Ready;
        existing->product = std::move(product);
        existing->failure = {};
        return *existing;
    }

    Result<RuntimeResourceRecord>
    RuntimeResourceRegistry::markFailed(RuntimeResourceTicket ticket,
                                        RuntimeResourceFailure failure) {
        if (!ticket) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidResourceKey,
                                     "Runtime resource failure update rejected invalid ticket.")};
        }

        if (failure.message.empty()) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::InvalidFailure,
                                     "Runtime resource failure update for " + keyLabel(ticket.key) +
                                         " rejected empty failure message.")};
        }

        auto existing =
            std::ranges::find_if(records_, [key = ticket.key](const RuntimeResourceRecord& record) {
                return record.key == key;
            });
        if (existing == records_.end()) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::MissingResource,
                "Runtime resource failure update could not find " + keyLabel(ticket.key) + ".")};
        }

        if (existing->generation != ticket.generation) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::GenerationMismatch,
                "Runtime resource failure update for " + keyLabel(ticket.key) +
                    " rejected stale generation expected=" + std::to_string(existing->generation) +
                    " actual=" + std::to_string(ticket.generation) + ".")};
        }

        if (existing->state != RuntimeResourceState::Pending) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::ResourceNotPending,
                "Runtime resource failure update for " + keyLabel(ticket.key) +
                    " rejected state=\"" + runtimeResourceStateName(existing->state) + "\".")};
        }

        existing->state = RuntimeResourceState::Failed;
        existing->product = {};
        existing->failure = std::move(failure);
        return *existing;
    }

    Result<RuntimeResourceRecord> RuntimeResourceRegistry::resolveProductRecords(
        RuntimeResourceTicket ticket, std::span<const asset::AssetProductRecord> products) {
        if (!ticket) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::InvalidResourceKey,
                "Runtime resource product resolution rejected invalid ticket.")};
        }

        auto existing =
            std::ranges::find_if(records_, [key = ticket.key](const RuntimeResourceRecord& record) {
                return record.key == key;
            });
        if (existing == records_.end()) {
            return std::unexpected{
                runtimeResourceError(RuntimeResourceDiagnosticCode::MissingResource,
                                     "Runtime resource product resolution could not find " +
                                         keyLabel(ticket.key) + ".")};
        }

        if (existing->generation != ticket.generation) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::GenerationMismatch,
                "Runtime resource product resolution for " + keyLabel(ticket.key) +
                    " rejected stale generation expected=" + std::to_string(existing->generation) +
                    " actual=" + std::to_string(ticket.generation) + ".")};
        }

        if (existing->state != RuntimeResourceState::Pending) {
            return std::unexpected{runtimeResourceError(
                RuntimeResourceDiagnosticCode::ResourceNotPending,
                "Runtime resource product resolution for " + keyLabel(ticket.key) +
                    " rejected state=\"" + runtimeResourceStateName(existing->state) + "\".")};
        }

        const asset::AssetProductRecord* invalidExpectedProduct = nullptr;
        const asset::AssetProductRecord* staleProduct = nullptr;
        for (const asset::AssetProductRecord& product : products) {
            if (product.key == existing->expectedProductKey) {
                if (product) {
                    return markReady(ticket, product);
                }

                invalidExpectedProduct = &product;
            } else if (productKeyHasResourceGuid(ticket.key, product.key)) {
                staleProduct = &product;
            }
        }

        if (invalidExpectedProduct != nullptr) {
            return markFailed(ticket,
                              RuntimeResourceFailure{
                                  .reason = RuntimeResourceFailureReason::InvalidProductRecord,
                                  .message = failedResolutionMessage(
                                      ticket.key, "found invalid expected " +
                                                      productKeyLabel(invalidExpectedProduct->key)),
                              });
        }

        if (staleProduct != nullptr) {
            return markFailed(
                ticket, RuntimeResourceFailure{
                            .reason = RuntimeResourceFailureReason::StaleProduct,
                            .message = failedResolutionMessage(
                                ticket.key, "found stale " + productKeyLabel(staleProduct->key) +
                                                " expected " +
                                                productKeyLabel(existing->expectedProductKey)),
                        });
        }

        return markFailed(ticket,
                          RuntimeResourceFailure{
                              .reason = RuntimeResourceFailureReason::MissingProduct,
                              .message = failedResolutionMessage(
                                  ticket.key, "could not find expected " +
                                                  productKeyLabel(existing->expectedProductKey)),
                          });
    }

    const RuntimeResourceRecord*
    RuntimeResourceRegistry::find(RuntimeResourceKey key) const noexcept {
        const auto found = std::ranges::find_if(
            records_, [key](const RuntimeResourceRecord& record) { return record.key == key; });
        return found == records_.end() ? nullptr : &*found;
    }

    std::span<const RuntimeResourceRecord> RuntimeResourceRegistry::records() const noexcept {
        return std::span<const RuntimeResourceRecord>{records_.data(), records_.size()};
    }

    const char* runtimeResourceStateName(RuntimeResourceState state) noexcept {
        switch (state) {
        case RuntimeResourceState::Pending:
            return "pending";
        case RuntimeResourceState::Ready:
            return "ready";
        case RuntimeResourceState::Failed:
            return "failed";
        }
        return "unknown";
    }

    const char* runtimeResourceDiagnosticCodeName(RuntimeResourceDiagnosticCode code) noexcept {
        switch (code) {
        case RuntimeResourceDiagnosticCode::InvalidResourceKey:
            return "invalid-resource-key";
        case RuntimeResourceDiagnosticCode::InvalidProductKey:
            return "invalid-product-key";
        case RuntimeResourceDiagnosticCode::MissingResource:
            return "missing-resource";
        case RuntimeResourceDiagnosticCode::GenerationMismatch:
            return "generation-mismatch";
        case RuntimeResourceDiagnosticCode::ProductKeyMismatch:
            return "product-key-mismatch";
        case RuntimeResourceDiagnosticCode::ResourceNotPending:
            return "resource-not-pending";
        case RuntimeResourceDiagnosticCode::InvalidFailure:
            return "invalid-failure";
        }
        return "unknown";
    }

    const char* runtimeResourceFailureReasonName(RuntimeResourceFailureReason reason) noexcept {
        switch (reason) {
        case RuntimeResourceFailureReason::MissingProduct:
            return "missing-product";
        case RuntimeResourceFailureReason::StaleProduct:
            return "stale-product";
        case RuntimeResourceFailureReason::InvalidProductRecord:
            return "invalid-product-record";
        case RuntimeResourceFailureReason::ProductReadFailed:
            return "product-read-failed";
        case RuntimeResourceFailureReason::UnsupportedProduct:
            return "unsupported-product";
        case RuntimeResourceFailureReason::RuntimeCreationFailed:
            return "runtime-creation-failed";
        }
        return "unknown";
    }

} // namespace asharia::resource

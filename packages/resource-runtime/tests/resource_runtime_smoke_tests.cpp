#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "asharia/resource_runtime/runtime_resource_registry.hpp"

namespace {

    struct SmokeTexture {};

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    bool messageExcludes(std::string_view message, std::string_view token) {
        return message.find(token) == std::string_view::npos;
    }

    asharia::asset::SourceAssetRecord makeTextureSource() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        const std::array settings{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
        };

        return asharia::asset::SourceAssetRecord{
            .guid = textureGuid ? *textureGuid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = "Content/Textures/Crate.png",
            .importerId = asharia::asset::makeImporterId(kTextureImporterName),
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0x1000F00D1234CAFEULL,
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    asharia::asset::AssetProductRecord
    makeTextureProduct(const asharia::asset::SourceAssetRecord& source) {
        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash("editor-debug");
        const asharia::asset::AssetProductKey key =
            asharia::asset::makeAssetProductKey(source, 0x6000ULL, targetProfile);
        return asharia::asset::AssetProductRecord{
            .key = key,
            .relativeProductPath = "textures/crate.texture-product",
            .productSizeBytes = 64,
            .productHash = asharia::asset::hashAssetProductKey(key),
        };
    }

    bool expectErrorCode(const asharia::Error& error,
                         asharia::resource::RuntimeResourceDiagnosticCode code,
                         std::string_view requiredMessageToken) {
        if (error.domain != asharia::ErrorDomain::Asset || error.code != static_cast<int>(code) ||
            !messageContains(error.message, requiredMessageToken) ||
            !messageExcludes(error.message, "Content/Textures")) {
            logFailure("Runtime resource smoke saw an unexpected diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeRuntimeResourceInvalids() {
        const asharia::asset::SourceAssetRecord source = makeTextureSource();
        const asharia::asset::AssetProductRecord product = makeTextureProduct(source);

        asharia::resource::RuntimeResourceRegistry registry;
        auto invalidRequest = registry.request({}, product.key);
        if (invalidRequest ||
            !expectErrorCode(invalidRequest.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::InvalidResourceKey,
                             "invalid resource key")) {
            return false;
        }

        asharia::resource::RuntimeResourceKey key{
            .guid = source.guid,
            .assetType = source.assetType,
        };
        asharia::asset::AssetProductKey mismatchedKey = product.key;
        mismatchedKey.assetType = asharia::asset::makeAssetTypeId("com.asharia.asset.Mesh");
        auto mismatchedRequest = registry.request(key, mismatchedKey);
        if (mismatchedRequest ||
            !expectErrorCode(mismatchedRequest.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::ProductKeyMismatch,
                             asharia::asset::formatAssetGuid(source.guid))) {
            return false;
        }

        auto emptyFailure = registry.markFailed(
            asharia::resource::RuntimeResourceTicket{.key = key, .generation = 1},
            asharia::resource::RuntimeResourceFailure{});
        if (emptyFailure ||
            !expectErrorCode(emptyFailure.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::InvalidFailure,
                             "empty failure message")) {
            return false;
        }

        return true;
    }

    bool smokeRuntimeResourceReadyAndFailed() {
        const asharia::asset::SourceAssetRecord source = makeTextureSource();
        const asharia::asset::AssetProductRecord product = makeTextureProduct(source);
        const asharia::asset::AssetHandle<SmokeTexture> textureHandle{.guid = source.guid};
        const asharia::resource::RuntimeResourceKey key =
            asharia::resource::makeRuntimeResourceKey(textureHandle, source.assetType);

        if (!key ||
            asharia::resource::runtimeResourceStateName(
                asharia::resource::RuntimeResourceState::Ready) != std::string_view{"ready"}) {
            logFailure("Runtime resource smoke saw invalid key or state label.");
            return false;
        }

        asharia::resource::RuntimeResourceRegistry registry;
        auto pending = registry.request(key, product.key);
        if (!pending || pending->generation != 1 || registry.records().size() != 1U) {
            logFailure("Runtime resource smoke failed pending request creation.");
            return false;
        }

        const asharia::resource::RuntimeResourceRecord* pendingRecord = registry.find(key);
        if (pendingRecord == nullptr ||
            pendingRecord->state != asharia::resource::RuntimeResourceState::Pending ||
            pendingRecord->expectedProductKey != product.key) {
            logFailure("Runtime resource smoke produced invalid pending state.");
            return false;
        }

        auto ready = registry.markReady(*pending, product);
        if (!ready || ready->state != asharia::resource::RuntimeResourceState::Ready ||
            ready->generation != pending->generation || ready->product != product ||
            registry.find(key)->state != asharia::resource::RuntimeResourceState::Ready) {
            logFailure("Runtime resource smoke failed ready transition.");
            return false;
        }

        auto notPending = registry.markReady(*pending, product);
        if (notPending ||
            !expectErrorCode(notPending.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::ResourceNotPending,
                             "state=\"ready\"")) {
            return false;
        }

        auto failedTicket = registry.request(key, product.key);
        if (!failedTicket || failedTicket->generation <= pending->generation) {
            logFailure("Runtime resource smoke failed second pending generation.");
            return false;
        }

        auto staleReady = registry.markReady(*pending, product);
        if (staleReady ||
            !expectErrorCode(staleReady.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::GenerationMismatch,
                             "stale generation")) {
            return false;
        }

        asharia::resource::RuntimeResourceFailure failure{
            .reason = asharia::resource::RuntimeResourceFailureReason::MissingProduct,
            .message = "product cache miss",
        };
        auto failed = registry.markFailed(*failedTicket, failure);
        if (!failed || failed->state != asharia::resource::RuntimeResourceState::Failed ||
            failed->failure != failure ||
            asharia::resource::runtimeResourceFailureReasonName(failed->failure.reason) !=
                std::string_view{"missing-product"}) {
            logFailure("Runtime resource smoke failed failed-state transition.");
            return false;
        }

        std::cout << "Runtime resource state: "
                  << asharia::resource::runtimeResourceStateName(failed->state)
                  << " generation=" << failed->generation << '\n';
        return true;
    }

    bool smokeRuntimeResourceProductMismatch() {
        const asharia::asset::SourceAssetRecord source = makeTextureSource();
        asharia::asset::SourceAssetRecord changedSource = source;
        changedSource.settingsHash ^= 0x10ULL;

        const asharia::asset::AssetProductRecord expectedProduct = makeTextureProduct(source);
        const asharia::asset::AssetProductRecord staleProduct = makeTextureProduct(changedSource);
        const asharia::resource::RuntimeResourceKey key{
            .guid = source.guid,
            .assetType = source.assetType,
        };

        asharia::resource::RuntimeResourceRegistry registry;
        auto pending = registry.request(key, expectedProduct.key);
        if (!pending) {
            logFailure(pending.error().message);
            return false;
        }

        auto mismatchedReady = registry.markReady(*pending, staleProduct);
        if (mismatchedReady ||
            !expectErrorCode(mismatchedReady.error(),
                             asharia::resource::RuntimeResourceDiagnosticCode::ProductKeyMismatch,
                             "expected productKey")) {
            return false;
        }

        if (asharia::resource::runtimeResourceDiagnosticCodeName(
                asharia::resource::RuntimeResourceDiagnosticCode::ProductKeyMismatch) !=
            std::string_view{"product-key-mismatch"}) {
            logFailure("Runtime resource smoke saw invalid diagnostic label.");
            return false;
        }

        return true;
    }

} // namespace

int main() {
    const bool passed = smokeRuntimeResourceInvalids() && smokeRuntimeResourceReadyAndFailed() &&
                        smokeRuntimeResourceProductMismatch();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

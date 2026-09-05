#include "asharia/resource_runtime/shader_resource.hpp"

#include <span>
#include <utility>

#include "asharia/asset_artifact/asset_artifact_v1.hpp"
#include "asharia/asset_pipeline/asset_product_blob.hpp"

namespace asharia::resource {
    namespace {
        Error shaderError(ShaderResourceError code, const ShaderResourceReadRequest& request,
                          const std::string& message) {
            return {ErrorDomain::Shader, static_cast<int>(code),
                    "Shader product '" + request.product.relativeProductPath + "' pass '" +
                        request.passName + "' stage '" + request.stage + "': " + message};
        }

        Result<const asset::AssetShaderCompileReflectionProductEntry*>
        selectEntry(std::span<const asset::AssetShaderCompileReflectionProductEntry> entries,
                    const ShaderResourceReadRequest& request) {
            const asset::AssetShaderCompileReflectionProductEntry* selected = nullptr;
            for (const auto& entry : entries) {
                if (entry.passName == request.passName && entry.stage == request.stage) {
                    if (selected != nullptr) {
                        return std::unexpected{shaderError(ShaderResourceError::EntryUnavailable,
                                                           request,
                                                           "ambiguous pass/stage entries")};
                    }
                    selected = &entry;
                }
            }
            if (selected == nullptr) {
                return std::unexpected{shaderError(ShaderResourceError::EntryUnavailable, request,
                                                   "no matching compiled entry")};
            }
            return selected;
        }
    } // namespace

    Result<ShaderResourceData> readShaderResource(const ShaderResourceReadRequest& request,
                                                  ShaderResourceReadLimits limits) {
        if (!request.guid || !request.product || request.artifactRoot.empty() ||
            request.targetProfile.empty() || request.stableTypeId.empty() ||
            request.passName.empty() || request.shaderProfile.empty() ||
            (request.stage != "vertex" && request.stage != "fragment" &&
             request.stage != "compute") ||
            limits.maxProductBytes == 0 || limits.maxSpirvBytes == 0 ||
            limits.maxReflectionBytes == 0 || limits.maxEntries == 0) {
            return std::unexpected{shaderError(ShaderResourceError::InvalidRequest, request,
                                               "missing identity, entry selection or limits")};
        }
        const auto& key = request.product.key;
        if (key.guid != request.guid ||
            key.assetType != asset::makeAssetTypeId("com.asharia.asset.Shader") ||
            key.importerId !=
                asset::makeImporterId("com.asharia.importer.shader-compile-reflection") ||
            key.importerVersion != asset::ImporterVersion{2} ||
            key.targetProfileHash != asset::makeAssetTargetProfileHash(request.targetProfile)) {
            return std::unexpected{
                shaderError(ShaderResourceError::IdentityMismatch, request,
                            "product record does not match the requested shader/target")};
        }
        auto locator = asset::makeAssetArtifactLocatorV1(request.product);
        if (!locator) {
            return std::unexpected{std::move(locator.error())};
        }
        auto artifact = asset::readVerifiedAssetArtifactV1(request.artifactRoot, *locator,
                                                           {.maxBytes = limits.maxProductBytes});
        if (!artifact) {
            return std::unexpected{std::move(artifact.error())};
        }
        // The existing blob reader consumes uint8 bytes. Copy explicitly instead of aliasing
        // std::byte storage through a type whose character-alias status is platform dependent.
        std::vector<std::uint8_t> bytes;
        bytes.reserve(artifact->bytes.size());
        for (const auto byte : artifact->bytes) {
            bytes.push_back(std::to_integer<std::uint8_t>(byte));
        }
        auto product = asset::readShaderCompileReflectionProductPayload(
            bytes, locator->relativePath,
            {.maxProductBytes = limits.maxProductBytes, .maxShaderEntries = limits.maxEntries});
        if (!product) {
            return std::unexpected{std::move(product.error())};
        }
        if (product->productKeyHash != asset::hashAssetProductKey(key) ||
            product->stableTypeId != request.stableTypeId || product->target != "spirv" ||
            product->profile != request.shaderProfile) {
            return std::unexpected{
                shaderError(ShaderResourceError::IdentityMismatch, request,
                            "compiled payload identity/profile differs from request")};
        }
        auto selected = selectEntry(product->entries, request);
        if (!selected) {
            return std::unexpected{std::move(selected.error())};
        }
        const auto& entry = **selected;
        if (entry.spirvBytes.size() > limits.maxSpirvBytes ||
            entry.reflectionJsonText.size() > limits.maxReflectionBytes) {
            return std::unexpected{shaderError(ShaderResourceError::BudgetExceeded, request,
                                               "selected entry exceeds byte budget")};
        }
        if (entry.slangcExitCode != 0 || entry.spirvValExitCode != 0 ||
            entry.compileEntryName.empty() || entry.spirvBytes.size() < 20 ||
            entry.spirvBytes.size() % 4 != 0) {
            return std::unexpected{
                shaderError(ShaderResourceError::InvalidCompiledEntry, request,
                            "entry is not a successful validated SPIR-V product")};
        }
        std::vector<std::uint32_t> spirv;
        spirv.reserve(entry.spirvBytes.size() / 4);
        for (std::size_t i = 0; i < entry.spirvBytes.size(); i += 4) {
            spirv.push_back(static_cast<std::uint32_t>(entry.spirvBytes[i]) |
                            (static_cast<std::uint32_t>(entry.spirvBytes[i + 1]) << 8U) |
                            (static_cast<std::uint32_t>(entry.spirvBytes[i + 2]) << 16U) |
                            (static_cast<std::uint32_t>(entry.spirvBytes[i + 3]) << 24U));
        }
        if (spirv[0] != 0x07230203U || spirv[3] == 0 || spirv[4] != 0) {
            return std::unexpected{shaderError(ShaderResourceError::InvalidCompiledEntry, request,
                                               "invalid SPIR-V header")};
        }
        auto reflection = parseShaderReflectionJson(entry.reflectionJsonText,
                                                    {.maxBytes = limits.maxReflectionBytes});
        if (!reflection) {
            return std::unexpected{shaderError(ShaderResourceError::InvalidCompiledEntry, request,
                                               reflection.error().message)};
        }
        if (reflection->stage != request.stage || reflection->entry != entry.compileEntryName ||
            reflection->target != product->target || reflection->profile != product->profile) {
            return std::unexpected{shaderError(ShaderResourceError::InvalidCompiledEntry, request,
                                               "reflection entry/stage/target/profile mismatch")};
        }
        return ShaderResourceData{.productKey = key,
                                  .productHash = request.product.productHash,
                                  .stableTypeId = std::move(product->stableTypeId),
                                  .passName = request.passName,
                                  .spirv = std::move(spirv),
                                  .reflection = std::move(*reflection)};
    }
} // namespace asharia::resource

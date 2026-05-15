#include "asharia/asset_core/asset_reference.hpp"

#include <expected>
#include <string>

namespace asharia::asset {
    namespace {

        [[nodiscard]] Error assetReferenceError(AssetReference reference,
                                                std::string_view sourcePath,
                                                std::string_view expectedTypeName,
                                                std::string_view actualTypeName,
                                                std::string_view reason) {
            return Error{
                ErrorDomain::Asset,
                2,
                "Invalid asset reference guid=\"" + formatAssetGuid(reference.guid) +
                    "\" source=\"" + std::string{sourcePath} + "\" expectedType=\"" +
                    std::string{expectedTypeName} + "\" actualType=\"" +
                    std::string{actualTypeName} + "\": " + std::string{reason},
            };
        }

    } // namespace

    VoidResult validateAssetReference(AssetReference reference,
                                      AssetTypeId actualType,
                                      std::string_view sourcePath,
                                      std::string_view expectedTypeName,
                                      std::string_view actualTypeName) {
        if (!reference.guid) {
            return std::unexpected{assetReferenceError(
                reference, sourcePath, expectedTypeName, actualTypeName, "asset GUID is invalid")};
        }

        if (!reference.expectedType) {
            return std::unexpected{assetReferenceError(reference,
                                                       sourcePath,
                                                       expectedTypeName,
                                                       actualTypeName,
                                                       "expected asset type is invalid")};
        }

        if (!actualType) {
            return std::unexpected{assetReferenceError(reference,
                                                       sourcePath,
                                                       expectedTypeName,
                                                       actualTypeName,
                                                       "actual asset type is invalid")};
        }

        if (reference.expectedType != actualType) {
            return std::unexpected{assetReferenceError(
                reference, sourcePath, expectedTypeName, actualTypeName, "asset type mismatch")};
        }

        return {};
    }

} // namespace asharia::asset

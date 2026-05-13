#include <cstdlib>
#include <iostream>
#include <string_view>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_type.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool expectInvalidGuid(std::string_view text) {
        auto parsed = asharia::asset::parseAssetGuid(text);
        if (parsed) {
            logFailure("Asset GUID smoke accepted an invalid GUID.");
            return false;
        }

        if (parsed.error().domain != asharia::ErrorDomain::Asset ||
            parsed.error().message.find(text) == std::string::npos) {
            logFailure("Asset GUID smoke produced an incomplete parse diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetGuid() {
        constexpr std::string_view kLowercaseGuid = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        auto parsed = asharia::asset::parseAssetGuid(kLowercaseGuid);
        if (!parsed) {
            logFailure(parsed.error().message);
            return false;
        }

        if (!*parsed || asharia::asset::formatAssetGuid(*parsed) != kLowercaseGuid) {
            logFailure("Asset GUID smoke failed lowercase UUID round-trip.");
            return false;
        }

        constexpr std::string_view kUppercaseGuid = "9F7A31A0-0B63-4D4C-9F18-BD9A0D2E9C21";
        auto parsedUppercase = asharia::asset::parseAssetGuid(kUppercaseGuid);
        if (!parsedUppercase ||
            asharia::asset::formatAssetGuid(*parsedUppercase) != kLowercaseGuid) {
            logFailure("Asset GUID smoke failed uppercase UUID canonicalization.");
            return false;
        }

        if (!expectInvalidGuid("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c2") ||
            !expectInvalidGuid("9f7a31a00-0b63-4d4c-9f18-bd9a0d2e9c21") ||
            !expectInvalidGuid("9f7a31a0_0b63-4d4c-9f18-bd9a0d2e9c21") ||
            !expectInvalidGuid("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c2x") ||
            !expectInvalidGuid("00000000-0000-0000-0000-000000000000")) {
            return false;
        }

        std::cout << "Asset GUID canonical: " << asharia::asset::formatAssetGuid(*parsed)
                  << '\n';
        return true;
    }

    bool smokeAssetType() {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";

        constexpr asharia::asset::AssetTypeId textureTypeA =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        constexpr asharia::asset::AssetTypeId textureTypeB =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        constexpr asharia::asset::AssetTypeId meshType =
            asharia::asset::makeAssetTypeId(kMeshTypeName);
        constexpr asharia::asset::AssetTypeId emptyType = asharia::asset::makeAssetTypeId("");

        if (!textureTypeA || textureTypeA != textureTypeB || textureTypeA == meshType ||
            emptyType) {
            logFailure("Asset type smoke saw unstable or invalid type identity.");
            return false;
        }

        std::cout << "Asset type id: " << textureTypeA.value << '\n';
        return true;
    }

} // namespace

int main() {
    const bool passed = smokeAssetGuid() && smokeAssetType();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

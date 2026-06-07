#pragma once

#include <span>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_core/asset_metadata.hpp"

namespace asharia::asset {

    inline constexpr std::string_view kTextureImportProfileSettingKey = "texture.profile";
    inline constexpr std::string_view kTextureSubAssetIdSettingPrefix = "texture.subAsset.";
    inline constexpr std::string_view kTextureSubAssetIdSettingSuffix = ".id";
    inline constexpr std::string_view kTextureSubAssetNameSettingSuffix = ".name";

    inline constexpr std::string_view kTextureImportProfileTexture2D = "texture2d";
    inline constexpr std::string_view kTextureImportProfileSpriteSheet = "sprite-sheet";
    inline constexpr std::string_view kTextureImportProfileTextureCube = "texture-cube";
    inline constexpr std::string_view kTextureImportProfileSkybox = "skybox";

    inline constexpr std::string_view kTextureRoleTexture2D = "com.asharia.asset.Texture2D";
    inline constexpr std::string_view kTextureRoleSpriteSheet = "com.asharia.asset.SpriteSheet";
    inline constexpr std::string_view kTextureRoleSprite = "com.asharia.asset.Sprite";
    inline constexpr std::string_view kTextureRoleTextureCube = "com.asharia.asset.TextureCube";
    inline constexpr std::string_view kTextureRoleSkybox = "com.asharia.asset.Skybox";

    [[nodiscard]] std::string normalizeTextureImportProfileName(std::string_view profileName);

    [[nodiscard]] AssetCatalogSourceFacet
    makeTextureImportCatalogSourceFacet(const SourceAssetRecord& source,
                                        std::span<const AssetImportSetting> settings);

} // namespace asharia::asset

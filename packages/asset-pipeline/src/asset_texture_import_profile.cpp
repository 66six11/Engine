#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asharia::asset {
    namespace {

        struct IndexedSubAsset {
            std::size_t index{};
            std::string stableId;
            std::string displayName;
        };

        [[nodiscard]] bool isAsciiSpace(char character) noexcept {
            return character == ' ' || character == '\t' || character == '\n' ||
                   character == '\r' || character == '\f' || character == '\v';
        }

        [[nodiscard]] std::string trimAscii(std::string_view value) {
            while (!value.empty() && isAsciiSpace(value.front())) {
                value.remove_prefix(1U);
            }
            while (!value.empty() && isAsciiSpace(value.back())) {
                value.remove_suffix(1U);
            }
            return std::string{value};
        }

        [[nodiscard]] std::string compactProfileToken(std::string_view value) {
            std::string token;
            token.reserve(value.size());
            for (char character : value) {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) == 0) {
                    continue;
                }
                token.push_back(static_cast<char>(std::tolower(byte)));
            }
            return token;
        }

        [[nodiscard]] const AssetImportSetting*
        findSetting(std::span<const AssetImportSetting> settings, std::string_view key) {
            const auto found = std::ranges::find_if(
                settings, [key](const AssetImportSetting& setting) { return setting.key == key; });
            return found == settings.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::optional<std::size_t>
        parseSubAssetSettingIndex(std::string_view key, std::string_view suffix) {
            if (!key.starts_with(kTextureSubAssetIdSettingPrefix) || !key.ends_with(suffix)) {
                return std::nullopt;
            }

            const std::size_t indexStart = kTextureSubAssetIdSettingPrefix.size();
            const std::size_t indexEnd = key.size() - suffix.size();
            if (indexEnd <= indexStart) {
                return std::nullopt;
            }

            std::size_t index = 0U;
            for (const char character : key.substr(indexStart, indexEnd - indexStart)) {
                if (character < '0' || character > '9') {
                    return std::nullopt;
                }
                index = (index * 10U) + static_cast<std::size_t>(character - '0');
            }
            return index;
        }

        [[nodiscard]] AssetCatalogDiagnostic
        sourceMetadataDiagnostic(const SourceAssetRecord& source, std::string message) {
            return AssetCatalogDiagnostic{
                .code = AssetCatalogDiagnosticCode::SourceMetadata,
                .severity = AssetCatalogDiagnosticSeverity::Warning,
                .guid = source.guid,
                .sourcePath = source.sourcePath,
                .message = std::move(message),
            };
        }

        [[nodiscard]] std::string roleForTextureProfile(std::string_view profile) {
            if (profile == kTextureImportProfileTexture2D) {
                return std::string{kTextureRoleTexture2D};
            }
            if (profile == kTextureImportProfileSpriteSheet) {
                return std::string{kTextureRoleSpriteSheet};
            }
            if (profile == kTextureImportProfileTextureCube) {
                return std::string{kTextureRoleTextureCube};
            }
            if (profile == kTextureImportProfileSkybox) {
                return std::string{kTextureRoleSkybox};
            }
            return {};
        }

        void appendSpriteSheetSubAssets(AssetCatalogSourceFacet& facet,
                                        const SourceAssetRecord& source,
                                        std::span<const AssetImportSetting> settings) {
            std::map<std::size_t, IndexedSubAsset> indexedSubAssets;
            for (const AssetImportSetting& setting : settings) {
                if (const std::optional<std::size_t> idIndex =
                        parseSubAssetSettingIndex(setting.key, kTextureSubAssetIdSettingSuffix)) {
                    indexedSubAssets
                        .try_emplace(
                            *idIndex,
                            IndexedSubAsset{.index = *idIndex, .stableId = {}, .displayName = {}})
                        .first->second.stableId = setting.value;
                    continue;
                }
                if (const std::optional<std::size_t> nameIndex =
                        parseSubAssetSettingIndex(setting.key, kTextureSubAssetNameSettingSuffix)) {
                    indexedSubAssets
                        .try_emplace(
                            *nameIndex,
                            IndexedSubAsset{.index = *nameIndex, .stableId = {}, .displayName = {}})
                        .first->second.displayName = setting.value;
                }
            }

            std::set<std::string, std::less<>> stableIds;
            for (const auto& [index, subAsset] : indexedSubAssets) {
                (void)index;
                if (subAsset.stableId.empty()) {
                    facet.diagnostics.push_back(sourceMetadataDiagnostic(
                        source, "Texture sprite-sheet sub-asset is missing a stable id."));
                    continue;
                }
                if (!stableIds.emplace(subAsset.stableId).second) {
                    facet.diagnostics.push_back(sourceMetadataDiagnostic(
                        source, "Texture sprite-sheet sub-asset has duplicate stable id '" +
                                    subAsset.stableId + "'."));
                    continue;
                }

                facet.subAssets.push_back(AssetCatalogSubAssetViewEntry{
                    .stableId = subAsset.stableId,
                    .displayName =
                        subAsset.displayName.empty() ? subAsset.stableId : subAsset.displayName,
                    .assetRoleName = std::string{kTextureRoleSprite},
                });
            }
        }

    } // namespace

    std::string normalizeTextureImportProfileName(std::string_view profileName) {
        const std::string token = compactProfileToken(profileName);
        if (token.empty()) {
            return {};
        }
        if (token == "texture2d" || token == "2d" || token == "image") {
            return std::string{kTextureImportProfileTexture2D};
        }
        if (token == "spritesheet" || token == "spriteatlas") {
            return std::string{kTextureImportProfileSpriteSheet};
        }
        if (token == "texturecube" || token == "cubemap" || token == "cube") {
            return std::string{kTextureImportProfileTextureCube};
        }
        if (token == "skybox") {
            return std::string{kTextureImportProfileSkybox};
        }
        return trimAscii(profileName);
    }

    AssetCatalogSourceFacet
    makeTextureImportCatalogSourceFacet(const SourceAssetRecord& source,
                                        std::span<const AssetImportSetting> settings) {
        AssetCatalogSourceFacet facet{
            .guid = source.guid,
            .sourcePath = source.sourcePath,
            .importProfileName = {},
            .assetRoleName = {},
            .subAssets = {},
            .diagnostics = {},
        };

        const AssetImportSetting* profileSetting =
            findSetting(settings, kTextureImportProfileSettingKey);
        if (profileSetting == nullptr || profileSetting->value.empty()) {
            return facet;
        }

        facet.importProfileName = normalizeTextureImportProfileName(profileSetting->value);
        facet.assetRoleName = roleForTextureProfile(facet.importProfileName);
        if (facet.assetRoleName.empty()) {
            facet.assetRoleName = "unknown";
            facet.diagnostics.push_back(sourceMetadataDiagnostic(
                source, "Texture import profile '" + profileSetting->value +
                            "' is unknown or unsupported."));
            return facet;
        }

        if (facet.importProfileName == kTextureImportProfileSpriteSheet) {
            appendSpriteSheetSubAssets(facet, source, settings);
        }

        return facet;
    }

} // namespace asharia::asset

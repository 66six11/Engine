#include "editor_asset_catalog_store.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"

namespace asharia::editor {
    namespace {

        struct FixtureSourceDesc {
            std::string_view guidText;
            std::string_view assetTypeName;
            std::string_view sourcePath;
            std::string_view importerName;
            std::uint64_t sourceHash{};
            std::uint64_t settingsHash{};
        };

        [[nodiscard]] asharia::asset::SourceAssetRecord
        fixtureSourceRecord(const FixtureSourceDesc& desc) {
            auto guid = asharia::asset::parseAssetGuid(desc.guidText);
            return asharia::asset::SourceAssetRecord{
                .guid = guid ? *guid : asharia::asset::AssetGuid{},
                .assetType = asharia::asset::makeAssetTypeId(desc.assetTypeName),
                .assetTypeName = std::string{desc.assetTypeName},
                .sourcePath = std::string{desc.sourcePath},
                .importerId = asharia::asset::makeImporterId(desc.importerName),
                .importerName = std::string{desc.importerName},
                .importerVersion = asharia::asset::ImporterVersion{1},
                .sourceHash = desc.sourceHash,
                .settingsHash = desc.settingsHash,
            };
        }

        [[nodiscard]] asharia::asset::AssetProductRecord
        fixtureProductRecord(const asharia::asset::SourceAssetRecord& source,
                             std::uint64_t dependencyHash, std::uint64_t targetProfileHash,
                             std::string_view relativeProductPath, std::uint64_t productSizeBytes) {
            const asharia::asset::AssetProductKey productKey =
                asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
            return asharia::asset::AssetProductRecord{
                .key = productKey,
                .relativeProductPath = std::string{relativeProductPath},
                .productSizeBytes = productSizeBytes,
                .productHash = asharia::asset::hashAssetProductKey(productKey),
            };
        }

        [[nodiscard]] asharia::asset::AssetCatalogView makeFixtureCatalogView() {
            constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
            constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
            constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
            constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture";
            constexpr std::string_view kTextTypeName = "com.asharia.asset.Text";
            const asharia::asset::SourceAssetRecord material =
                fixtureSourceRecord(FixtureSourceDesc{
                    .guidText = "b8373128-8e46-44e1-a5a4-df4c2ef9d2ad",
                    .assetTypeName = kMaterialTypeName,
                    .sourcePath = "Assets/Materials/brushed_metal.amat",
                    .importerName = "asharia.material",
                    .sourceHash = 0x1001ULL,
                    .settingsHash = 0x2001ULL,
                });
            const asharia::asset::SourceAssetRecord shader = fixtureSourceRecord(FixtureSourceDesc{
                .guidText = "13a10d4b-6987-48d1-ad27-ae4055e5a936",
                .assetTypeName = kShaderTypeName,
                .sourcePath = "Assets/Shaders/grid.slang",
                .importerName = "asharia.shader-slang",
                .sourceHash = 0x1002ULL,
                .settingsHash = 0x2002ULL,
            });
            asharia::asset::SourceAssetRecord staleMesh = fixtureSourceRecord(FixtureSourceDesc{
                .guidText = "1135c477-65aa-4d44-92f1-f208fc6142ad",
                .assetTypeName = kMeshTypeName,
                .sourcePath = "Assets/Meshes/cube.mesh",
                .importerName = "asharia.mesh-placeholder",
                .sourceHash = 0x1003ULL,
                .settingsHash = 0x2003ULL,
            });
            const asharia::asset::SourceAssetRecord texture = fixtureSourceRecord(FixtureSourceDesc{
                .guidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515",
                .assetTypeName = kTextureTypeName,
                .sourcePath = "Assets/Textures/checker.png",
                .importerName = "asharia.texture-placeholder",
                .sourceHash = 0x1004ULL,
                .settingsHash = 0x2004ULL,
            });
            const asharia::asset::SourceAssetRecord spriteSheet =
                fixtureSourceRecord(FixtureSourceDesc{
                    .guidText = "fd2e5880-dffb-4d27-b5d1-0c249005023a",
                    .assetTypeName = kTextureTypeName,
                    .sourcePath = "Assets/Textures/hero_sprites.png",
                    .importerName = "asharia.texture-placeholder",
                    .sourceHash = 0x1006ULL,
                    .settingsHash = 0x2006ULL,
                });
            const asharia::asset::SourceAssetRecord skybox = fixtureSourceRecord(FixtureSourceDesc{
                .guidText = "3b2cef92-bc92-43be-8e7d-a74a89c1d502",
                .assetTypeName = kTextureTypeName,
                .sourcePath = "Assets/Textures/studio_skybox.hdr",
                .importerName = "asharia.texture-placeholder",
                .sourceHash = 0x1007ULL,
                .settingsHash = 0x2007ULL,
            });
            const asharia::asset::SourceAssetRecord textureCube =
                fixtureSourceRecord(FixtureSourceDesc{
                    .guidText = "38fd0dc8-55ee-44c9-b12f-0179e0039c6b",
                    .assetTypeName = kTextureTypeName,
                    .sourcePath = "Assets/Textures/studio_probe_cube.ktx2",
                    .importerName = "asharia.texture-placeholder",
                    .sourceHash = 0x1008ULL,
                    .settingsHash = 0x2008ULL,
                });
            const asharia::asset::SourceAssetRecord note = fixtureSourceRecord(FixtureSourceDesc{
                .guidText = "f98f9d88-237f-4e8a-a4b6-9977d3a1fc2b",
                .assetTypeName = kTextTypeName,
                .sourcePath = "Assets/readme.md",
                .importerName = "asharia.text-placeholder",
                .sourceHash = 0x1005ULL,
                .settingsHash = 0x2005ULL,
            });

            asharia::asset::AssetCatalog catalog;
            if (!catalog.addSource(shader) || !catalog.addSource(note) ||
                !catalog.addSource(texture) || !catalog.addSource(spriteSheet) ||
                !catalog.addSource(skybox) || !catalog.addSource(textureCube) ||
                !catalog.addSource(material) || !catalog.addSource(staleMesh)) {
                return {};
            }

            const std::uint64_t profile =
                asharia::asset::makeAssetTargetProfileHash("editor-preview");
            asharia::asset::SourceAssetRecord oldMesh = staleMesh;
            oldMesh.sourceHash ^= 0x40ULL;
            const std::array expectedKeys{
                asharia::asset::makeAssetProductKey(material, 0x3001ULL, profile),
                asharia::asset::makeAssetProductKey(shader, 0x3002ULL, profile),
                asharia::asset::makeAssetProductKey(staleMesh, 0x3003ULL, profile),
                asharia::asset::makeAssetProductKey(texture, 0x3004ULL, profile),
                asharia::asset::makeAssetProductKey(note, 0x3005ULL, profile),
                asharia::asset::makeAssetProductKey(spriteSheet, 0x3006ULL, profile),
                asharia::asset::makeAssetProductKey(skybox, 0x3007ULL, profile),
                asharia::asset::makeAssetProductKey(textureCube, 0x3008ULL, profile),
            };
            const std::array products{
                fixtureProductRecord(material, 0x3001ULL, profile, "materials/material.product",
                                     512U),
                fixtureProductRecord(shader, 0x3002ULL, profile, "shaders/grid.product", 256U),
                fixtureProductRecord(texture, 0x3004ULL, profile, "textures/checker.product",
                                     1024U),
                fixtureProductRecord(spriteSheet, 0x3006ULL, profile, "textures/sprites.product",
                                     4096U),
                fixtureProductRecord(skybox, 0x3007ULL, profile, "textures/skybox.product", 8192U),
                fixtureProductRecord(textureCube, 0x3008ULL, profile, "textures/cube.product",
                                     8192U),
                fixtureProductRecord(oldMesh, 0x3003ULL, profile, "meshes/cube.old.product", 2048U),
            };
            const std::array textureSettings{asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTexture2D}}};
            const std::array spriteSettings{
                asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                    .value = std::string{asharia::asset::kTextureImportProfileSpriteSheet}},
                asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.id",
                                                   .value = "hero-idle-0"},
                asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.name",
                                                   .value = "Hero Idle 0"},
                asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.id",
                                                   .value = "hero-run-0"},
                asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.name",
                                                   .value = "Hero Run 0"},
            };
            const std::array skyboxSettings{asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSkybox}}};
            const std::array cubeSettings{asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTextureCube}}};
            const std::array facets{
                asharia::asset::makeTextureImportCatalogSourceFacet(texture, textureSettings),
                asharia::asset::makeTextureImportCatalogSourceFacet(spriteSheet, spriteSettings),
                asharia::asset::makeTextureImportCatalogSourceFacet(skybox, skyboxSettings),
                asharia::asset::makeTextureImportCatalogSourceFacet(textureCube, cubeSettings),
            };
            return asharia::asset::buildAssetCatalogView(
                catalog, products,
                asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                        .expectedProductKeys = expectedKeys,
                                                        .sourceFacets = facets});
        }

    } // namespace

    EditorAssetCatalogStore::EditorAssetCatalogStore()
        : fixtureCatalog_{makeFixtureCatalogView()} {}

    void EditorAssetCatalogStore::useFixtureCatalog() {
        snapshot_ = {};
        hasSnapshot_ = false;
    }

    void EditorAssetCatalogStore::useSnapshot(EditorAssetCatalogSnapshot snapshot) {
        snapshot_ = std::move(snapshot);
        hasSnapshot_ = true;
    }

    const asharia::asset::AssetCatalogView& EditorAssetCatalogStore::catalogView() const noexcept {
        return hasSnapshot_ ? snapshot_.catalogView : fixtureCatalog_;
    }

    const EditorAssetCatalogSnapshot* EditorAssetCatalogStore::snapshot() const noexcept {
        return hasSnapshot_ ? &snapshot_ : nullptr;
    }

    std::span<const EditorAssetCatalogDiagnostic>
    EditorAssetCatalogStore::diagnostics() const noexcept {
        return hasSnapshot_ ? std::span<const EditorAssetCatalogDiagnostic>{snapshot_.diagnostics}
                            : std::span<const EditorAssetCatalogDiagnostic>{};
    }

    const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store) {
        const EditorAssetCatalogSnapshot* snapshot = store.snapshot();
        return snapshot == nullptr ? nullptr
                                   : refreshEditorAssetCatalogStore(
                                         store, makeEditorAssetCatalogSnapshotRequest(*snapshot));
    }

    const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store,
                                   const EditorAssetCatalogSnapshotRequest& request) {
        store.useSnapshot(loadEditorAssetCatalogSnapshot(request));
        return store.snapshot();
    }

} // namespace asharia::editor

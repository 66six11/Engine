#include "editor_asset_catalog.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace asharia::editor {
    namespace {

        void addDiagnostic(EditorAssetCatalogSnapshot& snapshot,
                           EditorAssetCatalogDiagnosticCode code,
                           EditorAssetCatalogDiagnosticSeverity severity, std::string sourcePath,
                           std::filesystem::path path, std::string message) {
            snapshot.diagnostics.push_back(EditorAssetCatalogDiagnostic{
                .code = code,
                .severity = severity,
                .sourcePath = std::move(sourcePath),
                .path = std::move(path),
                .message = std::move(message),
            });
        }

        [[nodiscard]] std::filesystem::path
        projectDirectoryFor(const std::filesystem::path& projectFile) {
            const std::filesystem::path directory = projectFile.parent_path();
            return directory.empty() ? std::filesystem::path{"."} : directory;
        }

        [[nodiscard]] std::filesystem::path
        sourceRootPath(const std::filesystem::path& projectDirectory,
                       const asharia::project::AssetSourceRootDesc& root) {
            std::filesystem::path rootPath = std::filesystem::path{root.directory};
            if (rootPath.is_relative()) {
                rootPath = projectDirectory / rootPath;
            }
            return rootPath;
        }

        [[nodiscard]] asharia::asset::AssetProductManifestDocument
        readProductManifest(const EditorAssetCatalogSnapshotRequest& request,
                            EditorAssetCatalogSnapshot& snapshot) {
            if (request.productManifestFile.empty()) {
                return {};
            }

            auto manifest =
                asharia::asset::readAssetProductManifestFile(request.productManifestFile);
            if (!manifest) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ProductManifestReadFailed,
                              EditorAssetCatalogDiagnosticSeverity::Error, {},
                              request.productManifestFile,
                              "Editor asset catalog snapshot could not read product manifest: " +
                                  manifest.error().message);
                return {};
            }
            return std::move(*manifest);
        }

        void appendScanDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceScan,
                              EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                              diagnostic.sourceFilePath, diagnostic.message);
            }
        }

        void appendDiscoveryDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceDiscoveryDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceDiscovery,
                              EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                              diagnostic.metadataPath, diagnostic.message);
            }
        }

        void appendSnapshotDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceSnapshotDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceSnapshot,
                              EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                              diagnostic.sourceFilePath, diagnostic.message);
            }
        }

        [[nodiscard]] EditorAssetCatalogDiagnosticSeverity severityForImportPlanDiagnostic(
            asharia::asset::AssetImportPlanDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Info:
                return EditorAssetCatalogDiagnosticSeverity::Info;
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Warning:
                return EditorAssetCatalogDiagnosticSeverity::Warning;
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Error:
                return EditorAssetCatalogDiagnosticSeverity::Error;
            }
            return EditorAssetCatalogDiagnosticSeverity::Error;
        }

        void appendImportPlanDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetImportPlanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ImportPlanning,
                              severityForImportPlanDiagnostic(diagnostic.severity),
                              diagnostic.sourcePath, {}, diagnostic.message);
            }
        }

        [[nodiscard]] EditorAssetCatalogDiagnosticSeverity
        severityForCatalogDiagnostic(const asharia::asset::AssetCatalogDiagnostic& diagnostic) {
            switch (diagnostic.severity) {
            case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
                return EditorAssetCatalogDiagnosticSeverity::Info;
            case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
                return EditorAssetCatalogDiagnosticSeverity::Warning;
            case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
                return EditorAssetCatalogDiagnosticSeverity::Error;
            }
            return EditorAssetCatalogDiagnosticSeverity::Warning;
        }

        void appendCatalogViewDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetCatalogDiagnostic> diagnostics) {
            for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::CatalogView,
                              severityForCatalogDiagnostic(diagnostic), diagnostic.sourcePath, {},
                              diagnostic.message);
            }
        }

        void mergeSource(asharia::asset::AssetCatalog& catalog,
                         EditorAssetCatalogSnapshot& snapshot,
                         asharia::asset::SourceAssetRecord source) {
            const std::string sourcePath = source.sourcePath;
            auto added = catalog.addSource(std::move(source));
            if (!added) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::CatalogMerge,
                              EditorAssetCatalogDiagnosticSeverity::Error, sourcePath, {},
                              "Editor asset catalog snapshot could not merge source: " +
                                  added.error().message);
            }
        }

        void mergePlannedSources(asharia::asset::AssetCatalog& catalog,
                                 EditorAssetCatalogSnapshot& snapshot,
                                 const asharia::asset::AssetImportPlanResult& plan) {
            for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
                mergeSource(catalog, snapshot, hit.source);
            }
            for (const asharia::asset::AssetImportRequest& request : plan.requests) {
                mergeSource(catalog, snapshot, request.source);
            }
        }

        void
        appendExpectedProductKeys(std::vector<asharia::asset::AssetProductKey>& expectedProductKeys,
                                  const asharia::asset::AssetImportPlanResult& plan) {
            expectedProductKeys.reserve(expectedProductKeys.size() + plan.cacheHits.size() +
                                        plan.requests.size());
            for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
                expectedProductKeys.push_back(hit.product.key);
            }
            for (const asharia::asset::AssetImportRequest& request : plan.requests) {
                expectedProductKeys.push_back(request.productKey);
            }
        }

        void appendTextureProfileFacets(
            std::vector<asharia::asset::AssetCatalogSourceFacet>& sourceFacets,
            std::span<const asharia::asset::DiscoveredSourceAsset> discoveredSources) {
            sourceFacets.reserve(sourceFacets.size() + discoveredSources.size());
            for (const asharia::asset::DiscoveredSourceAsset& discovered : discoveredSources) {
                asharia::asset::AssetCatalogSourceFacet facet =
                    asharia::asset::makeTextureImportCatalogSourceFacet(discovered.source,
                                                                        discovered.settings);
                if (!facet.importProfileName.empty() || !facet.assetRoleName.empty() ||
                    !facet.subAssets.empty() || !facet.diagnostics.empty()) {
                    sourceFacets.push_back(std::move(facet));
                }
            }
        }

        void appendRootSnapshot(const std::filesystem::path& projectDirectory,
                                const asharia::project::AssetSourceRootDesc& root,
                                const asharia::asset::AssetProductManifestDocument& productManifest,
                                const std::string& targetProfile,
                                asharia::asset::AssetCatalog& catalog,
                                std::vector<asharia::asset::AssetProductKey>& expectedProductKeys,
                                std::vector<asharia::asset::AssetCatalogSourceFacet>& sourceFacets,
                                EditorAssetCatalogSnapshot& snapshot) {
            asharia::asset::AssetScannedImportPlanRequest request{
                .scan =
                    asharia::asset::AssetSourceScanRequest{
                        .sourceRoot = sourceRootPath(projectDirectory, root),
                        .sourcePathPrefix = root.sourcePathPrefix,
                        .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                        .ignoredDirectoryNames =
                            snapshot.project.assetDiscovery.ignoredDirectoryNames,
                    },
                .productManifest = productManifest,
                .targetProfile = targetProfile,
            };

            const asharia::asset::AssetScannedImportPlanResult result =
                asharia::asset::planScannedAssetImports(request);
            appendScanDiagnostics(snapshot, result.scan.diagnostics);
            appendDiscoveryDiagnostics(snapshot, result.discovery.diagnostics);
            appendSnapshotDiagnostics(snapshot, result.snapshot.diagnostics);
            appendImportPlanDiagnostics(snapshot, result.plan.diagnostics);
            appendTextureProfileFacets(sourceFacets, result.discovery.manifest.records);
            appendExpectedProductKeys(expectedProductKeys, result.plan);
            mergePlannedSources(catalog, snapshot, result.plan);
        }

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

    } // namespace

    EditorAssetCatalogStore::EditorAssetCatalogStore()
        : fixtureCatalog_{makeEditorAssetBrowserFixtureCatalogView()} {}

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

    std::string_view
    editorAssetCatalogDiagnosticCodeName(EditorAssetCatalogDiagnosticCode code) noexcept {
        switch (code) {
        case EditorAssetCatalogDiagnosticCode::InvalidRequest:
            return "invalid-request";
        case EditorAssetCatalogDiagnosticCode::ProjectDescriptorReadFailed:
            return "project-descriptor-read-failed";
        case EditorAssetCatalogDiagnosticCode::ProductManifestReadFailed:
            return "product-manifest-read-failed";
        case EditorAssetCatalogDiagnosticCode::SourceScan:
            return "source-scan";
        case EditorAssetCatalogDiagnosticCode::SourceDiscovery:
            return "source-discovery";
        case EditorAssetCatalogDiagnosticCode::SourceSnapshot:
            return "source-snapshot";
        case EditorAssetCatalogDiagnosticCode::ImportPlanning:
            return "import-planning";
        case EditorAssetCatalogDiagnosticCode::CatalogMerge:
            return "catalog-merge";
        case EditorAssetCatalogDiagnosticCode::CatalogView:
            return "catalog-view";
        }
        return "invalid-request";
    }

    std::string_view editorAssetCatalogDiagnosticSeverityName(
        EditorAssetCatalogDiagnosticSeverity severity) noexcept {
        switch (severity) {
        case EditorAssetCatalogDiagnosticSeverity::Info:
            return "info";
        case EditorAssetCatalogDiagnosticSeverity::Warning:
            return "warning";
        case EditorAssetCatalogDiagnosticSeverity::Error:
            return "error";
        }
        return "info";
    }

    bool EditorAssetCatalogSnapshot::succeeded() const noexcept {
        return std::ranges::none_of(
            diagnostics, [](const EditorAssetCatalogDiagnostic& diagnostic) {
                return diagnostic.severity == EditorAssetCatalogDiagnosticSeverity::Error;
            });
    }

    EditorAssetCatalogSnapshot
    loadEditorAssetCatalogSnapshot(const EditorAssetCatalogSnapshotRequest& request) {
        EditorAssetCatalogSnapshot snapshot{
            .projectFile = request.projectFile,
            .project = {},
            .catalogView = {},
            .diagnostics = {},
        };

        if (!request) {
            addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::InvalidRequest,
                          EditorAssetCatalogDiagnosticSeverity::Error, {}, request.projectFile,
                          "Editor asset catalog snapshot request requires a project file and "
                          "target profile.");
            return snapshot;
        }

        auto project = asharia::project::readAshariaProjectDescriptorFile(request.projectFile);
        if (!project) {
            addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ProjectDescriptorReadFailed,
                          EditorAssetCatalogDiagnosticSeverity::Error, {}, request.projectFile,
                          "Editor asset catalog snapshot could not read project descriptor: " +
                              project.error().message);
            return snapshot;
        }
        snapshot.project = std::move(*project);

        const asharia::asset::AssetProductManifestDocument productManifest =
            readProductManifest(request, snapshot);
        const std::filesystem::path projectDirectory = projectDirectoryFor(request.projectFile);
        asharia::asset::AssetCatalog catalog;
        std::vector<asharia::asset::AssetProductKey> expectedProductKeys;
        std::vector<asharia::asset::AssetCatalogSourceFacet> sourceFacets;
        for (const asharia::project::AssetSourceRootDesc& root :
             snapshot.project.assetSourceRoots) {
            appendRootSnapshot(projectDirectory, root, productManifest, request.targetProfile,
                               catalog, expectedProductKeys, sourceFacets, snapshot);
        }

        snapshot.catalogView = asharia::asset::buildAssetCatalogView(
            catalog, productManifest.products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
        appendCatalogViewDiagnostics(snapshot, snapshot.catalogView.diagnostics);
        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            appendCatalogViewDiagnostics(snapshot, entry.diagnostics);
        }
        return snapshot;
    }

    asharia::asset::AssetCatalogView makeEditorAssetBrowserFixtureCatalogView() {
        constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture";
        constexpr std::string_view kTextTypeName = "com.asharia.asset.Text";
        const asharia::asset::SourceAssetRecord material = fixtureSourceRecord(FixtureSourceDesc{
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
        const asharia::asset::SourceAssetRecord spriteSheet = fixtureSourceRecord(FixtureSourceDesc{
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
        const asharia::asset::SourceAssetRecord textureCube = fixtureSourceRecord(FixtureSourceDesc{
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
        if (!catalog.addSource(shader) || !catalog.addSource(note) || !catalog.addSource(texture) ||
            !catalog.addSource(spriteSheet) || !catalog.addSource(skybox) ||
            !catalog.addSource(textureCube) || !catalog.addSource(material) ||
            !catalog.addSource(staleMesh)) {
            return {};
        }

        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash("editor-preview");
        asharia::asset::SourceAssetRecord oldMesh = staleMesh;
        oldMesh.sourceHash ^= 0x40ULL;
        const std::array expectedProductKeys{
            asharia::asset::makeAssetProductKey(material, 0x3001ULL, targetProfile),
            asharia::asset::makeAssetProductKey(shader, 0x3002ULL, targetProfile),
            asharia::asset::makeAssetProductKey(staleMesh, 0x3003ULL, targetProfile),
            asharia::asset::makeAssetProductKey(texture, 0x3004ULL, targetProfile),
            asharia::asset::makeAssetProductKey(note, 0x3005ULL, targetProfile),
            asharia::asset::makeAssetProductKey(spriteSheet, 0x3006ULL, targetProfile),
            asharia::asset::makeAssetProductKey(skybox, 0x3007ULL, targetProfile),
            asharia::asset::makeAssetProductKey(textureCube, 0x3008ULL, targetProfile),
        };
        const std::array<asharia::asset::AssetProductRecord, 7> products{
            fixtureProductRecord(material, 0x3001ULL, targetProfile,
                                 "materials/brushed_metal.product", 512),
            fixtureProductRecord(shader, 0x3002ULL, targetProfile, "shaders/grid.product", 256),
            fixtureProductRecord(texture, 0x3004ULL, targetProfile, "textures/checker.product",
                                 1024),
            fixtureProductRecord(spriteSheet, 0x3006ULL, targetProfile,
                                 "textures/hero_sprites.product", 4096),
            fixtureProductRecord(skybox, 0x3007ULL, targetProfile, "textures/studio_skybox.product",
                                 8192),
            fixtureProductRecord(textureCube, 0x3008ULL, targetProfile,
                                 "textures/studio_probe_cube.product", 8192),
            fixtureProductRecord(oldMesh, 0x3003ULL, targetProfile, "meshes/cube.old.product",
                                 2048),
        };
        const std::array textureSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTexture2D},
            },
        };
        const std::array spriteSheetSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSpriteSheet},
            },
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.id",
                                               .value = "hero-idle-0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.name",
                                               .value = "Hero Idle 0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.id",
                                               .value = "hero-run-0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.name",
                                               .value = "Hero Run 0"},
        };
        const std::array skyboxSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSkybox},
            },
        };
        const std::array textureCubeSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTextureCube},
            },
        };
        const std::array sourceFacets{
            asharia::asset::makeTextureImportCatalogSourceFacet(texture, textureSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(spriteSheet, spriteSheetSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(skybox, skyboxSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(textureCube, textureCubeSettings),
        };

        return asharia::asset::buildAssetCatalogView(
            catalog, products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
    }

} // namespace asharia::editor

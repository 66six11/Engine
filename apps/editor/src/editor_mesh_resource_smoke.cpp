#include <array>
#include <bit>
#include <chrono>
#include <exception>
#include <future>
#include <stdexcept>
#include <vector>

#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_glb_import.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/core/file_io.hpp"
#include "asharia/core/log.hpp"
#include "asharia/project/project_descriptor_io.hpp"

#include "editor_mesh_resource.hpp"

namespace asharia::editor {
    namespace {
        void require(bool condition, const char* message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        }
        template <class T> T take(Result<T> value) {
            if (!value) {
                throw std::runtime_error(value.error().message);
            }
            return std::move(*value);
        }
        void check(VoidResult result) {
            if (!result) {
                throw std::runtime_error(result.error().message);
            }
        }
        struct Workspace {
            std::filesystem::path root;
            explicit Workspace(std::filesystem::path path) : root(std::move(path)) {}
            Workspace(const Workspace&) = delete;
            Workspace& operator=(const Workspace&) = delete;
            Workspace(Workspace&&) = delete;
            Workspace& operator=(Workspace&&) = delete;
            ~Workspace() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        };
        void appendWord(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
            for (unsigned shift = 0; shift < 32; shift += 8) {
                bytes.push_back(static_cast<std::uint8_t>((word >> shift) & 255U));
            }
        }
        std::vector<std::uint8_t> triangleGlb(float width) {
            std::string json = R"({"asset":{"version":"2.0"},"scene":0,
"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[)" +
                               std::to_string(width) + R"(,1,0]}]})";
            while (json.size() % 4U != 0) {
                json.push_back(' ');
            }
            std::vector<std::uint8_t> bytes;
            appendWord(bytes, 0x46546C67U);
            appendWord(bytes, 2U);
            appendWord(bytes, static_cast<std::uint32_t>(28U + json.size() + 36U));
            appendWord(bytes, static_cast<std::uint32_t>(json.size()));
            appendWord(bytes, 0x4E4F534AU);
            bytes.insert(bytes.end(), json.begin(), json.end());
            appendWord(bytes, 36U);
            appendWord(bytes, 0x004E4942U);
            for (const float value : std::array{0.F, 0.F, 0.F, width, 0.F, 0.F, 0.F, 1.F, 0.F}) {
                appendWord(bytes, std::bit_cast<std::uint32_t>(value));
            }
            return bytes;
        }
        asset::AssetProductRecord cook(const std::filesystem::path& root, asset::AssetGuid guid,
                                       float width) {
            const auto bytes = triangleGlb(width);
            check(core::writeFileBytesAtomically(root / "Assets/triangle.glb",
                                                 std::as_bytes(std::span{bytes})));
            const asset::SourceAssetRecord source{
                .guid = guid,
                .assetType = asset::makeAssetTypeId(mesh::kMeshAssetTypeName),
                .assetTypeName = std::string{mesh::kMeshAssetTypeName},
                .sourcePath = "Assets/triangle.glb",
                .importerId = asset::makeImporterId(asset::kGlbMeshImporterName),
                .importerName = std::string{asset::kGlbMeshImporterName},
                .importerVersion = asset::kGlbMeshImporterVersion,
                .sourceHash = asset::hashAssetArtifactBytesV1(std::as_bytes(std::span{bytes})),
                .settingsHash = asset::hashAssetImportSettings({})};
            check(asset::writeAssetMetadataFile(root / "Assets/triangle.glb.ameta",
                                                {.source = source, .settings = {}}));
            auto plan = asset::planScannedAssetImports(
                {.scan = {.sourceRoot = root / "Assets", .sourcePathPrefix = "Assets"},
                 .productManifest = {},
                 .targetProfile = "editor-preview",
                 .toolVersions = {}});
            require(plan.succeeded() && plan.plan.requests.size() == 1U, "GLB scan/plan failed");
            auto result = asset::executeAssetProducts(
                {.plan = std::move(plan.plan),
                 .existingManifest = {},
                 .sourceBytes = {{.sourcePath = source.sourcePath, .bytes = bytes}},
                 .dependencyProductBytes = {},
                 .productOutputRoot = root / "Cache",
                 .productManifestOutputPath = root / "Cache/products.json"});
            if (!result.succeeded()) {
                throw std::runtime_error(result.diagnostics.front().message);
            }
            require(result.writtenProducts.size() == 1U, "GLB cook produced no mesh");
            return result.writtenProducts.front().product;
        }
        resource::MeshResourceSnapshot
        complete(resource::MeshResourceStore& store,
                 const resource::MeshResourceRequestResult& request) {
            require(request.loadPlan.has_value(), "request did not produce a load plan");
            auto job = std::async(std::launch::async, [plan = *request.loadPlan] {
                return resource::loadMeshResourceCandidate(plan);
            });
            return take(store.publish(job.get()));
        }
        void run(const std::filesystem::path& root) {
            std::filesystem::create_directories(root / "Assets");
            const auto guid = take(asset::parseAssetGuid("43946ab3-7934-4e90-b510-a4a83b200421"));
            check(project::writeAshariaProjectDescriptorFile(
                root / "Mesh.asharia",
                {.projectName = "Mesh resource smoke",
                 .projectId = take(project::parseProjectId("43946ab3-7934-4e90-b510-a4a83b200422")),
                 .assetSourceRoots =
                     {{.rootName = "Assets", .directory = "Assets", .sourcePathPrefix = "Assets"}},
                 .assetCacheRoot = "Cache"}));
            const EditorAssetCatalogSnapshotRequest query{.projectFile = root / "Mesh.asharia",
                                                          .productManifestFile =
                                                              root / "Cache/products.json"};
            const auto firstRecord = cook(root, guid, 1.F);
            const auto first = loadEditorAssetCatalogSnapshot(query);
            auto store =
                take(resource::MeshResourceStore::create({.artifactRoot = root / "Cache"}));
            auto request = take(requestEditorMeshResource(first, guid, store));
            const auto pending = take(requestEditorMeshResource(first, guid, store));
            require(pending.disposition ==
                            resource::MeshResourceRequestDisposition::AlreadyPending &&
                        !pending.loadPlan,
                    "duplicate pending request started another load");
            require(complete(store, request).state == resource::MeshResourceState::Ready,
                    "initial mesh was not published");
            const auto oldLease = take(store.acquire(request.handle));
            require(oldLease.productHash() == firstRecord.productHash &&
                        oldLease.product().vertices().size() == 3U,
                    "CPU mesh lease lost geometry/identity");
            const auto ready = take(requestEditorMeshResource(first, guid, store));
            require(ready.disposition == resource::MeshResourceRequestDisposition::AlreadyReady &&
                        !ready.loadPlan,
                    "ready request performed redundant IO");
            require(!requestEditorMeshResource(first, {}, store), "invalid GUID accepted");

            const auto replacement = cook(root, guid, 2.F);
            const auto refreshed = loadEditorAssetCatalogSnapshot(query);
            auto reload = take(requestEditorMeshResource(refreshed, guid, store));
            const auto path = root / "Cache" / replacement.relativeProductPath;
            const auto goodBytes = take(core::readFileBytes(path, {.maxBytes = 1024ULL * 1024ULL}));
            check(core::writeFileBytesAtomically(path, std::span<const std::byte>{}));
            const auto failed = complete(store, reload);
            require(failed.lastFailure.has_value() &&
                        take(store.acquire(request.handle)).productHash() == oldLease.productHash(),
                    "failed reload replaced the active mesh");
            check(core::writeFileBytesAtomically(path, goodBytes));
            reload = take(requestEditorMeshResource(refreshed, guid, store));
            require(complete(store, reload).state == resource::MeshResourceState::Ready,
                    "valid replacement was not published");
            const auto newLease = take(store.acquire(request.handle));
            require(newLease.productHash() == replacement.productHash &&
                        newLease.revision() > oldLease.revision() &&
                        oldLease.productHash() == firstRecord.productHash,
                    "replacement corrupted old lease or failed to advance revision");
            check(store.unload(request.handle));
            require(!store.acquire(request.handle) && oldLease && newLease,
                    "unload did not invalidate handle or destroyed retained CPU leases");
        }
    } // namespace
    bool runEditorMeshResourceSmoke() {
        try {
            const auto base = std::filesystem::temp_directory_path();
            std::filesystem::path root;
            for (unsigned attempt = 0; attempt < 32; ++attempt) {
                root =
                    base /
                    ("editor-mesh-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(attempt));
                if (std::filesystem::create_directory(root)) {
                    break;
                }
                root.clear();
            }
            require(!root.empty(), "could not create isolated smoke workspace");
            const Workspace workspace{root};
            run(root);
            logInfo("Editor Mesh resource smoke: GLB -> catalog -> worker read -> CPU lease; "
                    "reuse, failed reload, replacement and unload passed.");
            return true;
        } catch (const std::exception& error) {
            logError(std::string{"Editor Mesh resource smoke: "} + error.what());
            return false;
        }
    }
} // namespace asharia::editor

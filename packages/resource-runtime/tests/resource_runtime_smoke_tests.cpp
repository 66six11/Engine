#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "asharia/core/file_io.hpp"
#include "asharia/mesh_product/mesh_product_writer_v1.hpp"
#include "asharia/resource_runtime/mesh_resource_store.hpp"

namespace {

    class ScopedDirectory final {
    public:
        ScopedDirectory()
            : path_(std::filesystem::temp_directory_path() /
                    "asharia-resource-runtime-tests-fixed") {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
            std::filesystem::create_directories(path_ / "products", error);
        }

        ~ScopedDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;
        ScopedDirectory(ScopedDirectory&&) = delete;
        ScopedDirectory& operator=(ScopedDirectory&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    struct SmokeMesh {};

    [[nodiscard]] bool contains(std::string_view text, std::string_view token) {
        return text.find(token) != std::string_view::npos;
    }

    [[nodiscard]] asharia::asset::AssetGuid makeGuid(std::uint8_t firstByte) {
        return asharia::asset::AssetGuid{.bytes = {firstByte}};
    }

    [[nodiscard]] asharia::asset::AssetProductKey makeProductKey(asharia::asset::AssetGuid guid,
                                                                 std::uint64_t sourceHash) {
        return asharia::asset::AssetProductKey{
            .guid = guid,
            .assetType = asharia::asset::makeAssetTypeId(asharia::mesh::kMeshAssetTypeName),
            .importerId = asharia::asset::makeImporterId("com.asharia.importer.mesh.test"),
            .importerVersion = asharia::asset::ImporterVersion{1U},
            .sourceHash = sourceHash,
            .settingsHash = 0x20U,
            .dependencyHash = 0U,
            .targetProfileHash = 0x30U,
        };
    }

    [[nodiscard]] asharia::mesh::MeshProductBuildInputV1 makeMesh(float maxX) {
        return asharia::mesh::MeshProductBuildInputV1{
            .vertices =
                {
                    {.positionX = 0.0F,
                     .positionY = 0.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 0.0F,
                     .uvY = 0.0F},
                    {.positionX = maxX,
                     .positionY = 0.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 1.0F,
                     .uvY = 0.0F},
                    {.positionX = 0.0F,
                     .positionY = 1.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 0.0F,
                     .uvY = 1.0F},
                },
            .indices = {0U, 1U, 2U},
            .submeshes = {{.firstIndex = 0U, .indexCount = 3U, .materialSlot = 0U}},
            .materialSlots = {{}},
            .bounds = {.minX = 0.0F,
                       .minY = 0.0F,
                       .minZ = 0.0F,
                       .maxX = maxX,
                       .maxY = 1.0F,
                       .maxZ = 0.0F},
        };
    }

    [[nodiscard]] asharia::Result<asharia::asset::AssetProductRecord>
    writeProduct(const std::filesystem::path& root, asharia::asset::AssetProductKey key,
                 std::string relativePath, float maxX) {
        auto bytes = asharia::mesh::writeMeshProductV1(makeMesh(maxX));
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }
        if (auto written = asharia::core::writeFileBytesAtomically(root / relativePath, *bytes);
            !written) {
            return std::unexpected{std::move(written.error())};
        }
        return asharia::asset::AssetProductRecord{
            .key = key,
            .relativeProductPath = std::move(relativePath),
            .productSizeBytes = bytes->size(),
            .productHash = asharia::asset::hashAssetArtifactBytesV1(*bytes),
        };
    }

    [[nodiscard]] asharia::resource::MeshResourceKey
    makeResourceKey(asharia::asset::AssetGuid guid) {
        return asharia::resource::makeMeshResourceKey(
            asharia::asset::AssetHandle<SmokeMesh>{.guid = guid});
    }

    [[nodiscard]] bool expectDiagnostic(const asharia::Error& error,
                                        asharia::resource::MeshResourceDiagnosticCode code,
                                        std::string_view token) {
        if (error.domain != asharia::ErrorDomain::Asset || error.code != static_cast<int>(code) ||
            !contains(error.message, token)) {
            std::cerr << "Unexpected resource diagnostic: " << error.message << '\n';
            return false;
        }
        return true;
    }

    [[nodiscard]] bool invalidAndSelectionTests(asharia::resource::MeshResourceStore& store,
                                                const std::filesystem::path& hiddenRoot) {
        const auto guid = makeGuid(0x11U);
        const auto key = makeResourceKey(guid);
        const auto productKey = makeProductKey(guid, 0x101U);

        auto invalid = store.request({}, productKey, {});
        if (invalid ||
            !expectDiagnostic(invalid.error(),
                              asharia::resource::MeshResourceDiagnosticCode::InvalidResourceKey,
                              "invalid resource key")) {
            return false;
        }

        auto wrongTypeKey = productKey;
        wrongTypeKey.assetType = asharia::asset::makeAssetTypeId("com.asharia.asset.Texture2D");
        auto wrongType = store.request(key, wrongTypeKey, {});
        if (wrongType ||
            !expectDiagnostic(wrongType.error(),
                              asharia::resource::MeshResourceDiagnosticCode::ProductTypeMismatch,
                              "non-mesh")) {
            return false;
        }

        auto missing = store.request(key, productKey, {});
        if (!missing ||
            missing->disposition !=
                asharia::resource::MeshResourceRequestDisposition::FailedNoActive ||
            !missing->failure) {
            std::cerr << "Missing product selection state differed.\n";
            return false;
        }
        const asharia::resource::MeshResourceFailure missingFailure =
            missing->failure.value_or(asharia::resource::MeshResourceFailure{});
        if (missingFailure.reason != asharia::resource::MeshResourceFailureReason::MissingProduct) {
            std::cerr << "Missing product failure reason differed.\n";
            return false;
        }

        auto staleKey = productKey;
        ++staleKey.sourceHash;
        const asharia::asset::AssetProductRecord staleRecord{
            .key = staleKey,
            .relativeProductPath = "products/stale.mesh",
            .productSizeBytes = 1U,
            .productHash = 1U,
        };
        auto stale = store.request(key, productKey, std::span{&staleRecord, 1U});
        if (!stale || !stale->failure) {
            std::cerr << "Stale product selection state differed.\n";
            return false;
        }
        const asharia::resource::MeshResourceFailure staleFailure =
            stale->failure.value_or(asharia::resource::MeshResourceFailure{});
        if (staleFailure.reason != asharia::resource::MeshResourceFailureReason::StaleProduct) {
            std::cerr << "Stale product failure reason differed.\n";
            return false;
        }

        const asharia::asset::AssetProductRecord invalidRecord{
            .key = productKey,
            .relativeProductPath = "../outside.mesh",
            .productSizeBytes = 1U,
            .productHash = 1U,
        };
        auto badRecord = store.request(key, productKey, std::span{&invalidRecord, 1U});
        if (!badRecord || !badRecord->failure) {
            std::cerr << "Invalid product record state or path redaction differed.\n";
            return false;
        }
        const asharia::resource::MeshResourceFailure& badRecordFailure = *badRecord->failure;
        if (badRecordFailure.reason !=
                asharia::resource::MeshResourceFailureReason::InvalidProductRecord ||
            contains(badRecordFailure.message, hiddenRoot.generic_string()) ||
            contains(badRecordFailure.message, "Content/")) {
            std::cerr << "Invalid product record failure or path redaction differed.\n";
            return false;
        }

        const std::array duplicateRecords{
            asharia::asset::AssetProductRecord{.key = productKey,
                                               .relativeProductPath = "products/first.mesh",
                                               .productSizeBytes = 1U,
                                               .productHash = 1U},
            asharia::asset::AssetProductRecord{.key = productKey,
                                               .relativeProductPath = "products/second.mesh",
                                               .productSizeBytes = 1U,
                                               .productHash = 2U},
        };
        auto duplicate = store.request(key, productKey, duplicateRecords);
        if (!duplicate || !duplicate->failure) {
            std::cerr << "Duplicate exact product records were not rejected.\n";
            return false;
        }
        const asharia::resource::MeshResourceFailure duplicateFailure =
            duplicate->failure.value_or(asharia::resource::MeshResourceFailure{});
        if (duplicateFailure.reason !=
                asharia::resource::MeshResourceFailureReason::InvalidProductRecord ||
            !contains(duplicateFailure.message, "duplicate exact")) {
            std::cerr << "Duplicate exact product failure differed.\n";
            return false;
        }
        return true;
    }

    // This test intentionally follows one resource through its complete linear lifecycle.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] bool lifecycleTests(asharia::resource::MeshResourceStore& store,
                                      const ScopedDirectory& directory) {
        const auto guid = makeGuid(0x22U);
        const auto key = makeResourceKey(guid);
        const auto keyA = makeProductKey(guid, 0x201U);
        auto productA = writeProduct(directory.path(), keyA, "products/a.mesh", 1.0F);
        if (!productA) {
            std::cerr << productA.error().message << '\n';
            return false;
        }

        auto requestA = store.request(key, keyA, std::span{&*productA, 1U});
        if (!requestA || !requestA->loadPlan ||
            requestA->disposition !=
                asharia::resource::MeshResourceRequestDisposition::LoadQueued) {
            std::cerr << "Initial mesh load was not queued.\n";
            return false;
        }
        auto duplicatePending = store.request(key, keyA, std::span{&*productA, 1U});
        if (!duplicatePending || duplicatePending->loadPlan ||
            duplicatePending->disposition !=
                asharia::resource::MeshResourceRequestDisposition::AlreadyPending) {
            std::cerr << "Pending mesh request was not deduplicated.\n";
            return false;
        }

        auto readyA =
            store.publish(asharia::resource::loadMeshResourceCandidate(*requestA->loadPlan));
        auto leaseA = store.acquire(requestA->handle);
        if (!readyA || readyA->state != asharia::resource::MeshResourceState::Ready ||
            readyA->activeRevision != 1U || !leaseA || leaseA->revision() != 1U ||
            leaseA->product().vertices().size() != 3U) {
            std::cerr << "Initial mesh publication or lease differed.\n";
            return false;
        }
        auto duplicateReady = store.request(key, keyA, std::span{&*productA, 1U});
        if (!duplicateReady ||
            duplicateReady->disposition !=
                asharia::resource::MeshResourceRequestDisposition::AlreadyReady) {
            std::cerr << "Ready mesh request was not deduplicated.\n";
            return false;
        }

        const auto keyB = makeProductKey(guid, 0x202U);
        auto productB = writeProduct(directory.path(), keyB, "products/b.mesh", 2.0F);
        const auto keyC = makeProductKey(guid, 0x203U);
        auto productC = writeProduct(directory.path(), keyC, "products/c.mesh", 3.0F);
        if (!productB || !productC) {
            std::cerr << "Could not prepare reload products.\n";
            return false;
        }

        auto requestB = store.request(key, keyB, std::span{&*productB, 1U});
        if (!requestB || !requestB->loadPlan) {
            return false;
        }
        const asharia::resource::MeshResourceLoadCompletion completionB =
            asharia::resource::loadMeshResourceCandidate(*requestB->loadPlan);
        auto requestC = store.request(key, keyC, std::span{&*productC, 1U});
        if (!requestC || !requestC->loadPlan) {
            return false;
        }
        auto stalePublish = store.publish(completionB);
        if (stalePublish ||
            !expectDiagnostic(
                stalePublish.error(),
                asharia::resource::MeshResourceDiagnosticCode::RequestGenerationMismatch,
                "stale request")) {
            return false;
        }

        auto pendingC = store.inspect(requestC->handle);
        auto stillA = store.acquire(requestC->handle);
        if (!pendingC || pendingC->state != asharia::resource::MeshResourceState::ReloadPending ||
            !stillA || stillA->revision() != 1U) {
            std::cerr << "Stale completion mutated the active or candidate revision.\n";
            return false;
        }

        auto readyC =
            store.publish(asharia::resource::loadMeshResourceCandidate(*requestC->loadPlan));
        auto leaseC = store.acquire(requestC->handle);
        if (!readyC || readyC->activeRevision != 2U || !leaseC || leaseC->revision() != 2U ||
            leaseC->product().bounds().maxX != 3.0F || leaseA->product().bounds().maxX != 1.0F) {
            std::cerr << "Successful reload did not preserve old lease or advance revision.\n";
            return false;
        }

        const auto keyD = makeProductKey(guid, 0x204U);
        const asharia::asset::AssetProductRecord missingArtifact{
            .key = keyD,
            .relativeProductPath = "products/missing.mesh",
            .productSizeBytes = productC->productSizeBytes,
            .productHash = productC->productHash,
        };
        auto requestD = store.request(key, keyD, std::span{&missingArtifact, 1U});
        if (!requestD || !requestD->loadPlan) {
            return false;
        }
        auto failedReload =
            store.publish(asharia::resource::loadMeshResourceCandidate(*requestD->loadPlan));
        auto retained = store.acquire(requestD->handle);
        if (!failedReload || failedReload->state != asharia::resource::MeshResourceState::Ready ||
            !failedReload->lastFailure || !retained || retained->revision() != 2U) {
            std::cerr << "Failed reload did not keep the active revision or redact the root.\n";
            return false;
        }
        const asharia::resource::MeshResourceFailure& reloadFailure = *failedReload->lastFailure;
        if (reloadFailure.reason !=
                asharia::resource::MeshResourceFailureReason::ArtifactReadFailed ||
            contains(reloadFailure.message, directory.path().generic_string())) {
            std::cerr << "Failed reload diagnostic differed or exposed the root.\n";
            return false;
        }

        const asharia::resource::MeshResourceHandle oldHandle = requestD->handle;
        if (auto unloaded = store.unload(oldHandle);
            !unloaded || !leaseC || leaseC->product().bounds().maxX != 3.0F) {
            std::cerr << "Unload invalidated an outstanding lease.\n";
            return false;
        }

        const auto otherGuid = makeGuid(0x33U);
        const auto otherKey = makeResourceKey(otherGuid);
        const auto otherProductKey = makeProductKey(otherGuid, 0x301U);
        auto otherProduct =
            writeProduct(directory.path(), otherProductKey, "products/other.mesh", 4.0F);
        if (!otherProduct) {
            return false;
        }
        auto reused = store.request(otherKey, otherProductKey, std::span{&*otherProduct, 1U});
        if (!reused || reused->handle.slot != oldHandle.slot ||
            reused->handle.slotGeneration == oldHandle.slotGeneration) {
            std::cerr << "Unloaded slot was not generation-safely reused.\n";
            return false;
        }
        auto oldAcquire = store.acquire(oldHandle);
        return !oldAcquire &&
               expectDiagnostic(
                   oldAcquire.error(),
                   asharia::resource::MeshResourceDiagnosticCode::SlotGenerationMismatch,
                   "stale handle");
    }

    [[nodiscard]] bool foreignStoreTest(const std::filesystem::path& root) {
        auto first = asharia::resource::MeshResourceStore::create({.artifactRoot = root});
        auto second = asharia::resource::MeshResourceStore::create({.artifactRoot = root});
        if (!first || !second) {
            return false;
        }
        const auto guid = makeGuid(0x77U);
        auto firstRequest = first->request(makeResourceKey(guid), makeProductKey(guid, 1U), {});
        auto secondRequest = second->request(makeResourceKey(guid), makeProductKey(guid, 1U), {});
        if (!firstRequest || !secondRequest || second->inspect(firstRequest->handle) ||
            second->unload(firstRequest->handle) || !second->inspect(secondRequest->handle)) {
            std::cerr << "Foreign store handle was accepted.\n";
            return false;
        }
        auto moved = std::move(*first);
        if (!moved.inspect(firstRequest->handle) || first->inspect(firstRequest->handle) ||
            first->request(makeResourceKey(guid), makeProductKey(guid, 1U), {})) {
            std::cerr << "Store move did not transfer exclusive identity.\n";
            return false;
        }
        auto product = writeProduct(root, makeProductKey(guid, 1U), "products/identity.mesh", 1.0F);
        if (!product) {
            return false;
        }
        auto pending = moved.request(makeResourceKey(guid), makeProductKey(guid, 1U),
                                     std::span{&*product, 1U});
        auto other = second->request(makeResourceKey(guid), makeProductKey(guid, 1U),
                                     std::span{&*product, 1U});
        if (!pending || !other || !pending->loadPlan) {
            return false;
        }
        auto completion = asharia::resource::loadMeshResourceCandidate(*pending->loadPlan);
        if (second->publish(completion) || !moved.publish(completion)) {
            return false;
        }
        *second = std::move(moved);
        // Intentional negative test of the documented moved-from rejection contract.
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        const bool movedFromRejected = !moved.inspect(pending->handle);
        return !second->inspect(secondRequest->handle) &&
               static_cast<bool>(second->acquire(pending->handle)) && movedFromRejected;
    }

    [[nodiscard]] bool ownerThreadTest(asharia::resource::MeshResourceStore& store) {
        asharia::Result<asharia::resource::MeshResourceRequestResult> result =
            std::unexpected{asharia::Error{}};
        std::thread worker([&store, &result]() {
            const auto guid = makeGuid(0x55U);
            result = store.request(makeResourceKey(guid), makeProductKey(guid, 0x501U), {});
        });
        worker.join();
        asharia::Result<asharia::resource::MeshResourceLease> read =
            std::unexpected{asharia::Error{}};
        asharia::Result<asharia::resource::MeshResourceSnapshot> snapshot =
            std::unexpected{asharia::Error{}};
        std::thread reader([&]() {
            read = store.acquire({});
            snapshot = store.inspect({});
        });
        reader.join();
        if (read || snapshot ||
            !expectDiagnostic(read.error(),
                              asharia::resource::MeshResourceDiagnosticCode::WrongOwnerThread,
                              "non-owner thread") ||
            !expectDiagnostic(snapshot.error(),
                              asharia::resource::MeshResourceDiagnosticCode::WrongOwnerThread,
                              "non-owner thread")) {
            return false;
        }
        return !result &&
               expectDiagnostic(result.error(),
                                asharia::resource::MeshResourceDiagnosticCode::WrongOwnerThread,
                                "non-owner thread");
    }

} // namespace

// Unexpected exceptions are reported by the test executable rather than escaping main.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        const ScopedDirectory directory;
        auto store = asharia::resource::MeshResourceStore::create(
            {.artifactRoot = directory.path(), .artifactLimits = {}, .meshLimits = {}});
        if (!store) {
            std::cerr << store.error().message << '\n';
            return EXIT_FAILURE;
        }
        if (!invalidAndSelectionTests(*store, directory.path()) ||
            !lifecycleTests(*store, directory) || !foreignStoreTest(directory.path()) ||
            !ownerThreadTest(*store)) {
            return EXIT_FAILURE;
        }
        std::cout << "Mesh resource runtime tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Mesh resource runtime tests threw: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Mesh resource runtime tests caught an unknown exception.\n";
        return EXIT_FAILURE;
    }
}

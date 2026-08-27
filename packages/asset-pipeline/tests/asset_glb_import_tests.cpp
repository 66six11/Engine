#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_type.hpp"
#include "asharia/asset_pipeline/asset_glb_import.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"

namespace {

    struct TestContext {
        int failures{};

        void expect(bool condition, std::string_view message) {
            if (!condition) {
                std::cerr << "GLB importer test failed: " << message << '\n';
                ++failures;
            }
        }
    };

    void appendUint32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    }

    void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
        appendUint32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    void appendVertex(std::vector<std::uint8_t>& bytes, float positionX, float positionY,
                      float positionZ, float normalX, float normalY, float normalZ, float textureU,
                      float textureV) {
        appendFloat(bytes, positionX);
        appendFloat(bytes, positionY);
        appendFloat(bytes, positionZ);
        appendFloat(bytes, normalX);
        appendFloat(bytes, normalY);
        appendFloat(bytes, normalZ);
        appendFloat(bytes, textureU);
        appendFloat(bytes, textureV);
    }

    [[nodiscard]] std::vector<std::uint8_t> makeGlb(std::string json,
                                                    std::vector<std::uint8_t> bin) {
        while (json.size() % 4U != 0U) {
            json.push_back(' ');
        }
        while (bin.size() % 4U != 0U) {
            bin.push_back(0U);
        }

        const auto totalBytes =
            static_cast<std::uint32_t>(12U + 8U + json.size() + 8U + bin.size());
        std::vector<std::uint8_t> glb;
        glb.reserve(totalBytes);
        appendUint32(glb, 0x46546C67U);
        appendUint32(glb, 2U);
        appendUint32(glb, totalBytes);
        appendUint32(glb, static_cast<std::uint32_t>(json.size()));
        appendUint32(glb, 0x4E4F534AU);
        glb.insert(glb.end(), json.begin(), json.end());
        appendUint32(glb, static_cast<std::uint32_t>(bin.size()));
        appendUint32(glb, 0x004E4942U);
        glb.insert(glb.end(), bin.begin(), bin.end());
        return glb;
    }

    struct Fixture {
        std::string json;
        std::vector<std::uint8_t> bin;
    };

    [[nodiscard]] Fixture makeFixture(std::uint32_t indexComponentType) {
        std::vector<std::uint8_t> bin;
        appendVertex(bin, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F);
        appendVertex(bin, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
        appendVertex(bin, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F);

        std::uint32_t indexBytes = 0U;
        if (indexComponentType == 5121U) {
            bin.insert(bin.end(), {0U, 1U, 2U});
            indexBytes = 3U;
        } else if (indexComponentType == 5123U) {
            bin.insert(bin.end(), {0U, 0U, 1U, 0U, 2U, 0U});
            indexBytes = 6U;
        } else {
            appendUint32(bin, 0U);
            appendUint32(bin, 1U);
            appendUint32(bin, 2U);
            indexBytes = 12U;
        }
        while (bin.size() % 4U != 0U) {
            bin.push_back(0U);
        }
        const auto secondPositionOffset = static_cast<std::uint32_t>(bin.size());
        appendFloat(bin, 0.0F);
        appendFloat(bin, 0.0F);
        appendFloat(bin, 1.0F);
        appendFloat(bin, 1.0F);
        appendFloat(bin, 0.0F);
        appendFloat(bin, 1.0F);
        appendFloat(bin, 0.0F);
        appendFloat(bin, 1.0F);
        appendFloat(bin, 1.0F);

        std::string json =
            R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)"
            R"("nodes":[{"mesh":0,"translation":[2,0,0]},{"mesh":1,"scale":[-1,1,1]}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]},{"primitives":[{"attributes":{"POSITION":4},"material":1}]}],)"
            R"("materials":[{},{}],)"
            R"("buffers":[{"byteLength":)" +
            std::to_string(bin.size()) +
            R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32,"target":34962},{"buffer":0,"byteOffset":96,"byteLength":)" +
            std::to_string(indexBytes) + R"(,"target":34963},{"buffer":0,"byteOffset":)" +
            std::to_string(secondPositionOffset) +
            R"(,"byteLength":36,"target":34962}],)"
            R"("accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
            R"({"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},)"
            R"({"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},)"
            R"({"bufferView":1,"componentType":)" +
            std::to_string(indexComponentType) +
            R"(,"count":3,"type":"SCALAR"},)"
            R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,1],"max":[1,1,1]}]})";
        return Fixture{.json = std::move(json), .bin = std::move(bin)};
    }

    [[nodiscard]] asharia::asset::AssetGlbImportRequest makeRequest(const Fixture& fixture) {
        return asharia::asset::AssetGlbImportRequest{
            .source =
                asharia::asset::SourceAssetRecord{
                    .guid = asharia::asset::AssetGuid{.bytes = {0x38U, 0x6U}},
                    .assetType = asharia::asset::makeAssetTypeId(asharia::mesh::kMeshAssetTypeName),
                    .assetTypeName = std::string{asharia::mesh::kMeshAssetTypeName},
                    .sourcePath = "Content/Models/restricted-static-mesh.glb",
                    .importerId =
                        asharia::asset::makeImporterId(asharia::asset::kGlbMeshImporterName),
                    .importerName = std::string{asharia::asset::kGlbMeshImporterName},
                    .importerVersion = asharia::asset::kGlbMeshImporterVersion,
                    .sourceHash = 0x1234U,
                    .settingsHash = 0x5678U,
                },
            .sourceBytes = makeGlb(fixture.json, fixture.bin),
            .limits = {},
        };
    }

    [[nodiscard]] bool nearlyEqual(float lhs, float rhs) {
        return std::fabs(lhs - rhs) <= 1.0e-6F;
    }

    [[nodiscard]] std::uint64_t hashSourceBytes(std::span<const std::uint8_t> bytes) noexcept {
        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
        std::uint64_t hash = kFnv1a64Offset;
        for (const std::uint8_t byte : bytes) {
            hash ^= byte;
            hash *= kFnv1a64Prime;
        }
        return hash;
    }

    [[nodiscard]] asharia::asset::AssetProductExecutionResult
    executeImportRequest(asharia::asset::AssetGlbImportRequest request) {
        constexpr std::string_view kTargetProfile = "glb-import-test";
        request.source.sourceHash = hashSourceBytes(request.sourceBytes);
        request.source.settingsHash = asharia::asset::hashAssetImportSettings(request.settings);
        const std::uint64_t targetProfileHash =
            asharia::asset::makeAssetTargetProfileHash(kTargetProfile);
        const std::vector<asharia::asset::AssetDependency> dependencies;
        const std::uint64_t dependencyHash = asharia::asset::hashAssetDependencies(dependencies);
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(request.source, dependencyHash, targetProfileHash);
        const std::string productPath =
            asharia::asset::makeAssetImportProductPath(productKey, kTargetProfile);
        return asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
            .plan =
                asharia::asset::AssetImportPlanResult{
                    .targetProfile = std::string{kTargetProfile},
                    .targetProfileHash = targetProfileHash,
                    .requests =
                        {
                            asharia::asset::AssetImportRequest{
                                .source = request.source,
                                .settings = request.settings,
                                .dependencies = dependencies,
                                .productKey = productKey,
                                .relativeProductPath = productPath,
                                .reason = asharia::asset::AssetImportRequestReason::MissingProduct,
                            },
                        },
                    .cacheHits = {},
                    .diagnostics = {},
                },
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = request.source.sourcePath,
                        .bytes = std::move(request.sourceBytes),
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = "AssetGlbImportExecutionTestOutput",
            .productManifestOutputPath = {},
        });
    }

    void expectDiagnostic(TestContext& testContext,
                          const asharia::Result<asharia::mesh::MeshProductBuildInputV1>& result,
                          asharia::asset::AssetGlbImportDiagnosticCode code,
                          std::string_view message) {
        testContext.expect(!result, message);
        if (!result) {
            testContext.expect(result.error().domain == asharia::ErrorDomain::Asset,
                               "diagnostic must use Asset error domain");
            if (result.error().code != static_cast<int>(code)) {
                std::cerr << "Expected diagnostic "
                          << asharia::asset::assetGlbImportDiagnosticCodeName(code) << " but got "
                          << result.error().code << ": " << result.error().message << '\n';
                testContext.expect(false, "diagnostic must preserve the typed importer code");
            }
            testContext.expect(!result.error().message.empty(), "diagnostic must be actionable");
        }
    }

    void expectExecutionMeshImportFailure(TestContext& testContext,
                                          const asharia::asset::AssetProductExecutionResult& result,
                                          std::string_view message) {
        testContext.expect(!result.succeeded(), message);
        testContext.expect(result.diagnostics.size() == 1U,
                           "invalid GLB execution must emit exactly one diagnostic");
        if (result.diagnostics.size() == 1U) {
            testContext.expect(
                result.diagnostics.front().code ==
                    asharia::asset::AssetProductExecutionDiagnosticCode::MeshImportFailed,
                "invalid GLB execution must retain MeshImportFailed routing identity");
        }
        testContext.expect(result.writtenProducts.empty(),
                           "invalid GLB execution must not publish a placeholder product");
        testContext.expect(!result.manifestWritten,
                           "invalid GLB execution must not publish a product manifest");
    }

    void testSupportedIndexTypesAndDeterminism(TestContext& testContext) {
        for (const std::uint32_t componentType : {5121U, 5123U, 5125U}) {
            const Fixture fixture = makeFixture(componentType);
            const auto request = makeRequest(fixture);
            const auto first = asharia::asset::importRestrictedGlbMesh(request);
            const auto second = asharia::asset::importRestrictedGlbMesh(request);
            testContext.expect(first.has_value(), "u8/u16/u32 indexed fixture must import");
            testContext.expect(second.has_value(), "repeated import must succeed");
            if (!first || !second) {
                continue;
            }
            testContext.expect(*first == *second,
                               "same source must produce deterministic normalized DTO");
            const auto productBytes = asharia::mesh::writeMeshProductV1(*first);
            testContext.expect(
                productBytes.has_value(),
                "normalized DTO must satisfy canonical Mesh Product v1 writer invariants");
            testContext.expect(first->vertices.size() == 6U,
                               "missing normals must split the second triangle");
            testContext.expect(first->indices ==
                                   std::vector<std::uint32_t>({0U, 2U, 1U, 3U, 4U, 5U}),
                               "mirror determinant must flip only the first triangle winding");
            testContext.expect(first->submeshes.size() == 2U && first->materialSlots.size() == 3U,
                               "source-order primitives and material slots must be retained");
            testContext.expect(
                first->submeshes[0].firstIndex == 0U && first->submeshes[0].indexCount == 3U &&
                    first->submeshes[0].materialSlot == 1U &&
                    first->submeshes[1].firstIndex == 3U && first->submeshes[1].materialSlot == 2U,
                "submesh ranges and material slot mapping must be stable");
            testContext.expect(
                nearlyEqual(first->vertices[0].positionX, -2.0F) &&
                    nearlyEqual(first->vertices[1].positionX, -3.0F) &&
                    nearlyEqual(first->vertices[0].normalZ, 1.0F),
                "node translation, interleaved accessor offset, mirror, and normal must bake");
            testContext.expect(nearlyEqual(first->vertices[3].normalX, 0.0F) &&
                                   nearlyEqual(first->vertices[3].normalY, 0.0F) &&
                                   nearlyEqual(first->vertices[3].normalZ, 1.0F) &&
                                   nearlyEqual(first->vertices[3].uvX, 0.0F) &&
                                   nearlyEqual(first->vertices[3].uvY, 0.0F),
                               "missing normals must be flat and missing UVs must be zero");
            testContext.expect(
                nearlyEqual(first->bounds.minX, -3.0F) && nearlyEqual(first->bounds.maxX, 1.0F) &&
                    nearlyEqual(first->bounds.minZ, 0.0F) && nearlyEqual(first->bounds.maxZ, 1.0F),
                "bounds must be recomputed from baked positions");
        }
    }

    void replaceOnce(TestContext& testContext, std::string& text, std::string_view from,
                     std::string_view replacement) {
        const std::size_t offset = text.find(from);
        testContext.expect(offset != std::string::npos, "test fixture mutation token must exist");
        if (offset != std::string::npos) {
            text.replace(offset, from.size(), replacement);
        }
    }

    void testRejectedSubsetAndLimits(TestContext& testContext) {
        Fixture uri = makeFixture(5121U);
        replaceOnce(testContext, uri.json, R"("buffers":[{)",
                    R"("buffers":[{"uri":"data:application/octet-stream;base64,AA==",)");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(uri)),
                         asharia::asset::AssetGlbImportDiagnosticCode::ExternalUriUnsupported,
                         "any URI must be rejected before semantic loading");

        Fixture extension = makeFixture(5121U);
        replaceOnce(
            testContext, extension.json, R"("scene":0,)",
            R"("extensionsUsed":["EXT_fixture"],"extensionsRequired":["EXT_fixture"],"scene":0,)");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(extension)),
                         asharia::asset::AssetGlbImportDiagnosticCode::RequiredExtensionUnsupported,
                         "required extensions must be rejected");

        Fixture topology = makeFixture(5121U);
        replaceOnce(testContext, topology.json, R"("indices":3,"material":0)",
                    R"("indices":3,"material":0,"mode":1)");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(topology)),
                         asharia::asset::AssetGlbImportDiagnosticCode::UnsupportedPrimitiveTopology,
                         "non-TRIANGLES primitives must be rejected");

        Fixture noDefaultScene = makeFixture(5121U);
        replaceOnce(testContext, noDefaultScene.json, R"("scene":0,)", "");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(noDefaultScene)),
                         asharia::asset::AssetGlbImportDiagnosticCode::MissingDefaultScene,
                         "a default scene is required");

        Fixture depth = makeFixture(5121U);
        replaceOnce(testContext, depth.json, R"("scenes":[{"nodes":[0,1]}])",
                    R"("scenes":[{"nodes":[0]}])");
        replaceOnce(
            testContext, depth.json,
            R"("nodes":[{"mesh":0,"translation":[2,0,0]},{"mesh":1,"scale":[-1,1,1]}])",
            R"("nodes":[{"mesh":0,"translation":[2,0,0],"children":[1]},{"mesh":1,"scale":[-1,1,1]}])");
        auto depthRequest = makeRequest(depth);
        depthRequest.limits.maxNodeDepth = 1U;
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(depthRequest),
                         asharia::asset::AssetGlbImportDiagnosticCode::CountLimitExceeded,
                         "node depth must be bounded without recursive traversal");

        Fixture animation = makeFixture(5121U);
        replaceOnce(testContext, animation.json, R"("materials":[{},{}],)",
                    R"("animations":[{}],"materials":[{},{}],)");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(animation)),
                         asharia::asset::AssetGlbImportDiagnosticCode::AnimationUnsupported,
                         "animations must be rejected before semantic loading");

        Fixture skin = makeFixture(5121U);
        replaceOnce(testContext, skin.json, R"("materials":[{},{}],)",
                    R"("skins":[{}],"materials":[{},{}],)");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(skin)),
                         asharia::asset::AssetGlbImportDiagnosticCode::SkinUnsupported,
                         "skins must be rejected before semantic loading");

        Fixture camera = makeFixture(5121U);
        replaceOnce(
            testContext, camera.json, R"("nodes":[{"mesh":0)",
            R"("cameras":[{"type":"perspective","perspective":{"yfov":1,"znear":0.1}}],"nodes":[{"mesh":0,"camera":0)");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(camera)),
                         asharia::asset::AssetGlbImportDiagnosticCode::SceneSemanticUnsupported,
                         "camera scene semantics must fail closed");

        Fixture light = makeFixture(5121U);
        replaceOnce(
            testContext, light.json, R"("asset":{"version":"2.0"})",
            R"("asset":{"version":"2.0"},"extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[{"type":"directional"}]}})");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(light)),
                         asharia::asset::AssetGlbImportDiagnosticCode::SceneSemanticUnsupported,
                         "light scene semantics must fail closed");

        Fixture morph = makeFixture(5121U);
        replaceOnce(testContext, morph.json, R"("indices":3,"material":0)",
                    R"("indices":3,"material":0,"targets":[{"POSITION":0}])");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(morph)),
                         asharia::asset::AssetGlbImportDiagnosticCode::MorphTargetUnsupported,
                         "morph targets must be rejected before semantic loading");

        Fixture sparse = makeFixture(5121U);
        replaceOnce(testContext, sparse.json, R"("count":3,"type":"VEC3","min":[0,0,0])",
                    R"("count":3,"type":"VEC3","sparse":{"count":1},"min":[0,0,0])");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(makeRequest(sparse)),
                         asharia::asset::AssetGlbImportDiagnosticCode::SparseAccessorUnsupported,
                         "sparse accessors must be rejected before semantic loading");

        Fixture unknownAttribute = makeFixture(5121U);
        replaceOnce(testContext, unknownAttribute.json, R"("TEXCOORD_0":2)",
                    R"("TEXCOORD_0":2,"_FIXTURE":2)");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(unknownAttribute)),
                         asharia::asset::AssetGlbImportDiagnosticCode::UnsupportedVertexAttribute,
                         "attributes outside the restricted matrix must be rejected");

        Fixture outOfRange = makeFixture(5121U);
        outOfRange.bin[98U] = 3U;
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(outOfRange)),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidIndex,
                         "indices outside POSITION count must be rejected");

        Fixture nonFinite = makeFixture(5121U);
        const auto infinityBits = std::bit_cast<std::uint32_t>(INFINITY);
        nonFinite.bin[0U] = static_cast<std::uint8_t>(infinityBits & 0xFFU);
        nonFinite.bin[1U] = static_cast<std::uint8_t>((infinityBits >> 8U) & 0xFFU);
        nonFinite.bin[2U] = static_cast<std::uint8_t>((infinityBits >> 16U) & 0xFFU);
        nonFinite.bin[3U] = static_cast<std::uint8_t>((infinityBits >> 24U) & 0xFFU);
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(nonFinite)),
                         asharia::asset::AssetGlbImportDiagnosticCode::NonFiniteValue,
                         "non-finite decoded positions must be rejected");

        Fixture degenerate = makeFixture(5121U);
        for (std::size_t offset = 112U; offset < 136U; ++offset) {
            degenerate.bin[offset] = 0U;
        }
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(degenerate)),
                         asharia::asset::AssetGlbImportDiagnosticCode::DegenerateTriangle,
                         "flat-normal generation must reject degenerate triangles");

        Fixture authoredNormalDegenerate = makeFixture(5121U);
        for (std::size_t offset = 0U; offset < 12U; ++offset) {
            authoredNormalDegenerate.bin[64U + offset] = authoredNormalDegenerate.bin[32U + offset];
        }
        expectDiagnostic(
            testContext,
            asharia::asset::importRestrictedGlbMesh(makeRequest(authoredNormalDegenerate)),
            asharia::asset::AssetGlbImportDiagnosticCode::DegenerateTriangle,
            "authored normals must not hide degenerate triangles");
    }

    void testRequestAndByteLimits(TestContext& testContext) {
        const Fixture fixture = makeFixture(5121U);
        auto sourceLimit = makeRequest(fixture);
        sourceLimit.limits.maxSourceBytes = sourceLimit.sourceBytes.size() - 1U;
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(sourceLimit),
                         asharia::asset::AssetGlbImportDiagnosticCode::SourceByteLimitExceeded,
                         "source byte limit must apply before parsing");

        auto decodeLimit = makeRequest(fixture);
        decodeLimit.limits.maxDecodedBytes = 35U;
        const auto decodeLimitResult = asharia::asset::importRestrictedGlbMesh(decodeLimit);
        expectDiagnostic(testContext, decodeLimitResult,
                         asharia::asset::AssetGlbImportDiagnosticCode::CountLimitExceeded,
                         "decoded accessor allocations must be bounded");
        if (!decodeLimitResult) {
            testContext.expect(
                decodeLimitResult.error().message.find("material slots") != std::string::npos,
                "decoded-byte material slot gate must run before materialSlots resize");
        }

        auto materialLimit = makeRequest(fixture);
        materialLimit.limits.maxMaterialSlots = 2U;
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(materialLimit),
                         asharia::asset::AssetGlbImportDiagnosticCode::CountLimitExceeded,
                         "material slot count must be bounded before output allocation");

        auto extension = makeRequest(fixture);
        extension.source.sourcePath = "Content/Models/restricted-static-mesh.gltf";
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(extension),
                         asharia::asset::AssetGlbImportDiagnosticCode::UnsupportedSourceExtension,
                         "only .glb sources are accepted");

        auto malformed = makeRequest(fixture);
        malformed.sourceBytes.pop_back();
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(malformed),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidGlb,
                         "declared GLB length must exactly match source bytes");

        auto zeroJsonDepth = makeRequest(fixture);
        zeroJsonDepth.limits.maxJsonNestingDepth = 0U;
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(zeroJsonDepth),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "zero JSON nesting depth must be rejected as an invalid limit");

        auto wrongAssetTypeName = makeRequest(fixture);
        wrongAssetTypeName.source.assetTypeName = "com.asharia.asset.Texture2D";
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(wrongAssetTypeName),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "mesh importer must reject a non-mesh asset type name");

        auto wrongAssetTypeId = makeRequest(fixture);
        wrongAssetTypeId.source.assetType =
            asharia::asset::makeAssetTypeId("com.asharia.asset.Texture2D");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(wrongAssetTypeId),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "mesh importer must reject a non-mesh asset type id");

        auto unsupportedSettings = makeRequest(fixture);
        unsupportedSettings.settings.push_back(
            asharia::asset::AssetImportSetting{.key = "fixture", .value = "unsupported"});
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(unsupportedSettings),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "importer version 1 must reject non-empty settings");

        auto wrongImporterId = makeRequest(fixture);
        wrongImporterId.source.importerId = asharia::asset::makeImporterId("fixture.importer");
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(wrongImporterId),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "mesh importer must reject a mismatched importer id");

        auto wrongImporterName = makeRequest(fixture);
        wrongImporterName.source.importerName = "fixture.importer";
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(wrongImporterName),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "mesh importer must reject a mismatched importer name");

        auto wrongImporterVersion = makeRequest(fixture);
        wrongImporterVersion.source.importerVersion = asharia::asset::ImporterVersion{2U};
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(wrongImporterVersion),
                         asharia::asset::AssetGlbImportDiagnosticCode::InvalidRequest,
                         "mesh importer must reject a mismatched importer version");
    }

    void testBoundedJsonPreflight(TestContext& testContext) {
        Fixture deepNesting = makeFixture(5121U);
        std::string nestedValue;
        constexpr std::size_t kNestedArrayCount = 64U;
        nestedValue.reserve((kNestedArrayCount * 2U) + 1U);
        nestedValue.append(kNestedArrayCount, '[');
        nestedValue.push_back('0');
        nestedValue.append(kNestedArrayCount, ']');
        replaceOnce(testContext, deepNesting.json, R"("asset":{"version":"2.0"})",
                    R"("asset":{"version":"2.0"},"nested":)" + nestedValue);
        auto depthRequest = makeRequest(deepNesting);
        depthRequest.limits.maxJsonNestingDepth = 16U;
        expectDiagnostic(testContext, asharia::asset::importRestrictedGlbMesh(depthRequest),
                         asharia::asset::AssetGlbImportDiagnosticCode::CountLimitExceeded,
                         "JSON nesting must fail with a typed limit before recursive parsing");

        Fixture escapedUri = makeFixture(5121U);
        replaceOnce(testContext, escapedUri.json, R"("buffers":[{)",
                    R"("buffers":[{"u\u0072i":"fixture.bin",)");
        expectDiagnostic(testContext,
                         asharia::asset::importRestrictedGlbMesh(makeRequest(escapedUri)),
                         asharia::asset::AssetGlbImportDiagnosticCode::ExternalUriUnsupported,
                         "escaped JSON member names must not bypass the URI policy");
    }

    void testCandidateRouting(TestContext& testContext) {
        const asharia::asset::SourceAssetRecord baseline{};

        auto extension = baseline;
        extension.sourcePath = "Content/Meshes/Fixture.GLB";
        testContext.expect(asharia::asset::isRestrictedGlbMeshImportCandidate(extension),
                           "case-insensitive .glb extension must select mesh import");

        auto importerName = baseline;
        importerName.importerName = std::string{asharia::asset::kGlbMeshImporterName};
        testContext.expect(asharia::asset::isRestrictedGlbMeshImportCandidate(importerName),
                           "expected importer name must select mesh import");

        auto importerId = baseline;
        importerId.importerId =
            asharia::asset::makeImporterId(asharia::asset::kGlbMeshImporterName);
        testContext.expect(asharia::asset::isRestrictedGlbMeshImportCandidate(importerId),
                           "expected importer id must select mesh import");

        auto assetTypeName = baseline;
        assetTypeName.assetTypeName = std::string{asharia::mesh::kMeshAssetTypeName};
        testContext.expect(asharia::asset::isRestrictedGlbMeshImportCandidate(assetTypeName),
                           "Mesh asset type name must select mesh import");

        auto assetTypeId = baseline;
        assetTypeId.assetType = asharia::asset::makeAssetTypeId(asharia::mesh::kMeshAssetTypeName);
        testContext.expect(asharia::asset::isRestrictedGlbMeshImportCandidate(assetTypeId),
                           "Mesh asset type id must select mesh import");

        auto unrelated = baseline;
        unrelated.sourcePath = "Content/Textures/Fixture.png";
        unrelated.importerId = asharia::asset::makeImporterId("fixture.importer");
        unrelated.importerName = "fixture.importer";
        unrelated.assetType = asharia::asset::makeAssetTypeId("com.asharia.asset.Texture2D");
        unrelated.assetTypeName = "com.asharia.asset.Texture2D";
        testContext.expect(!asharia::asset::isRestrictedGlbMeshImportCandidate(unrelated),
                           "unrelated source contracts must not select mesh import");
    }

    void testExecutionRouting(TestContext& testContext) {
        const Fixture fixture = makeFixture(5121U);

        auto wrongImporter = makeRequest(fixture);
        wrongImporter.source.importerId = asharia::asset::makeImporterId("fixture.importer");
        wrongImporter.source.importerName = "fixture.importer";
        expectExecutionMeshImportFailure(
            testContext, executeImportRequest(std::move(wrongImporter)),
            "a .glb with a wrong importer must fail through the mesh execution route");

        auto wrongAssetType = makeRequest(fixture);
        wrongAssetType.source.assetType =
            asharia::asset::makeAssetTypeId("com.asharia.asset.Texture2D");
        wrongAssetType.source.assetTypeName = "com.asharia.asset.Texture2D";
        expectExecutionMeshImportFailure(
            testContext, executeImportRequest(std::move(wrongAssetType)),
            "a .glb with a wrong asset type must fail through the mesh execution route");

        auto unsupportedSettings = makeRequest(fixture);
        unsupportedSettings.settings.push_back(
            asharia::asset::AssetImportSetting{.key = "fixture", .value = "unsupported"});
        expectExecutionMeshImportFailure(
            testContext, executeImportRequest(std::move(unsupportedSettings)),
            "a .glb with settings must fail through the mesh execution route");
    }

} // namespace

int main() noexcept {
    try {
        TestContext testContext;
        testSupportedIndexTypesAndDeterminism(testContext);
        testRejectedSubsetAndLimits(testContext);
        testRequestAndByteLimits(testContext);
        testBoundedJsonPreflight(testContext);
        testCandidateRouting(testContext);
        testExecutionRouting(testContext);

        if (testContext.failures != 0) {
            return EXIT_FAILURE;
        }
        std::cout << "Restricted GLB importer tests passed.\n";
        return EXIT_SUCCESS;
    } catch (...) {
        return EXIT_FAILURE;
    }
}

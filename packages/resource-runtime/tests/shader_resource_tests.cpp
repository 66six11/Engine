#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "asharia/asset_artifact/asset_artifact_v1.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"
#include "asharia/core/file_io.hpp"
#include "asharia/editor_content/asset_catalog_snapshot.hpp"
#include "asharia/project/project_descriptor_io.hpp"
#include "asharia/resource_runtime/shader_resource.hpp"

namespace {
    using namespace asharia;
    using namespace asharia::asset;
    using namespace asharia::resource;

    void require(bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    template <typename T> T take(Result<T> value) {
        if (!value) {
            throw std::runtime_error(value.error().message);
        }
        return std::move(*value);
    }

    std::uint64_t hash(std::span<const std::uint8_t> bytes) {
        return hashAssetArtifactBytesV1(std::as_bytes(bytes));
    }

    void verifyCatalog(const std::filesystem::path& root, const SourceAssetRecord& source,
                       const std::vector<AssetImportSetting>& settings,
                       const std::vector<std::uint8_t>& bytes, const AssetImportRequest& planned,
                       const std::vector<AssetDependency>& productDependencies,
                       const AssetProductRecord& product) {
        std::filesystem::create_directories(root / "Content");
        require(core::writeFileBytesAtomically(root / source.sourcePath,
                                               std::as_bytes(std::span{bytes}))
                    .has_value(),
                "Could not write catalog source");
        require(writeAssetMetadataFile(root / (source.sourcePath + ".ameta"),
                                       {.source = source, .settings = settings})
                    .has_value(),
                "Could not write catalog metadata");
        require(
            project::writeAshariaProjectDescriptorFile(
                root / "asharia.project.json",
                {.projectName = "ShaderCatalog",
                 .projectId = take(project::parseProjectId("11111111-2222-3333-4444-555555555555")),
                 .assetSourceRoots = {{.rootName = "Content",
                                       .directory = "Content",
                                       .sourcePathPrefix = "Content"}},
                 .assetCacheRoot = "Cache"})
                .has_value(),
            "Could not write catalog project");
        editor::EditorAssetCatalogSnapshotRequest request{
            .projectFile = root / "asharia.project.json",
            .productManifestFile = root / "compiled.json",
            .targetProfile = "test-native",
            .productDependencies = productDependencies};
        for (const auto& dependency : planned.dependencies) {
            if (dependency.kind == AssetDependencyKind::ToolVersion) {
                request.toolVersions.push_back({.importerId = source.importerId,
                                                .toolName = dependency.path,
                                                .versionHash = dependency.hash});
            }
        }
        const auto snapshot = editor::loadEditorAssetCatalogSnapshot(request);
        const auto selected =
            take(editor::selectEditorAssetProduct(snapshot, source.guid, source.assetType));
        require(selected == product, "Catalog selection differs from the compiled product");
        request.productDependencies.front().hash++;
        const auto stale = editor::loadEditorAssetCatalogSnapshot(request);
        require(!editor::selectEditorAssetProduct(stale, source.guid, source.assetType),
                "Catalog accepted compiled product after upstream dependency changed");
    }

    AssetProductWrite cook(const std::filesystem::path& root,
                           const std::vector<std::uint8_t>& sourceBytes,
                           const AssetProductWrite* dependency = nullptr) {
        std::vector<AssetImportSetting> settings;
        const std::string importer = dependency == nullptr
                                         ? "com.asharia.importer.shader-authoring"
                                         : "com.asharia.importer.shader-compile-reflection";
        if (dependency != nullptr) {
            settings.push_back({.key = "shader.authoringProductPath",
                                .value = dependency->product.relativeProductPath});
        }
        const SourceAssetRecord source{
            .guid = take(parseAssetGuid("69bc6326-c04a-49d8-a4d2-653445a0e423")),
            .assetType = makeAssetTypeId("com.asharia.asset.Shader"),
            .assetTypeName = "com.asharia.asset.Shader",
            .sourcePath = "Content/Unlit.shader",
            .importerId = makeImporterId(importer),
            .importerName = importer,
            .importerVersion = ImporterVersion{2},
            .sourceHash = hash(sourceBytes),
            .settingsHash = hashAssetImportSettings(settings),
        };
        std::vector<AssetDependency> productDependencies;
        std::vector<AssetProductDependencyBytes> dependencyBytes;
        if (dependency != nullptr) {
            productDependencies.push_back({.owner = source.guid,
                                           .kind = AssetDependencyKind::AssetReference,
                                           .path = dependency->product.relativeProductPath,
                                           .hash = dependency->product.productHash});
            const auto input = take(core::readFileBytes(dependency->productFilePath,
                                                        {.maxBytes = 64ULL * 1024ULL * 1024ULL}));
            std::vector<std::uint8_t> bytes;
            bytes.reserve(input.size());
            for (const auto byte : input) {
                bytes.push_back(std::to_integer<std::uint8_t>(byte));
            }
            dependencyBytes.push_back(
                {.relativeProductPath = dependency->product.relativeProductPath,
                 .productHash = dependency->product.productHash,
                 .bytes = std::move(bytes)});
        }
        const std::array sources{
            DiscoveredSourceAsset{.entry = {.sourcePath = source.sourcePath, .metadataPath = {}},
                                  .source = source,
                                  .settings = settings}};
        const std::array snapshots{AssetSourceSnapshot{.sourcePath = source.sourcePath,
                                                       .sourceFilePath = {},
                                                       .sourceHash = source.sourceHash}};
        const auto plan = planAssetImports(sources, snapshots, {}, "test-native",
                                           {.productDependencies = productDependencies});
        require(plan.succeeded() && plan.requests.size() == 1U,
                "Generic planner did not produce the shader cook request");
        if (dependency != nullptr) {
            const auto missing = planAssetImports(sources, snapshots, {}, "test-native");
            require(missing.requests.empty() && missing.cacheHits.empty(),
                    "Planner proved a compiled key without the authoring product dependency");
            auto changedDependencies = productDependencies;
            changedDependencies.front().hash++;
            const auto changedPlan = planAssetImports(sources, snapshots, {}, "test-native",
                                                      {.productDependencies = changedDependencies});
            require(changedPlan.requests.size() == 1U &&
                        changedPlan.requests.front().productKey != plan.requests.front().productKey,
                    "Changed authoring product did not invalidate compiled key");
            changedDependencies = productDependencies;
            changedDependencies.push_back(changedDependencies.front());
            const auto duplicate = planAssetImports(sources, snapshots, {}, "test-native",
                                                    {.productDependencies = changedDependencies});
            require(!duplicate.succeeded() && duplicate.requests.empty(),
                    "Duplicate product dependency was accepted");
            changedDependencies = productDependencies;
            changedDependencies.front().path = "../escape.product";
            const auto invalid = planAssetImports(sources, snapshots, {}, "test-native",
                                                  {.productDependencies = changedDependencies});
            require(!invalid.succeeded() && invalid.requests.empty(),
                    "Escaping product dependency path was accepted");
        }
        const auto result = executeAssetProducts({
            .plan = plan,
            .existingManifest = {},
            .sourceBytes = {{.sourcePath = source.sourcePath, .bytes = sourceBytes}},
            .dependencyProductBytes = std::move(dependencyBytes),
            .productOutputRoot = root,
            .productManifestOutputPath =
                root / (dependency == nullptr ? "authoring.json" : "compiled.json"),
        });
        if (!result.succeeded()) {
            throw std::runtime_error(result.diagnostics.front().message);
        }
        require(result.writtenProducts.size() == 1, "expected one cooked shader product");
        if (dependency != nullptr) {
            verifyCatalog(root, source, settings, sourceBytes, plan.requests.front(),
                          productDependencies, result.writtenProducts.front().product);
        }
        return result.writtenProducts.front();
    }

    void expectShaderError(const Result<ShaderResourceData>& result, ShaderResourceError code) {
        require(!result && result.error().domain == ErrorDomain::Shader &&
                    result.error().code == static_cast<int>(code),
                "unexpected shader error");
    }

    void run() {
        const std::filesystem::path root{ASHARIA_SHADER_RESOURCE_TEST_ROOT};
        std::filesystem::create_directories(root);
        const std::string source = R"shader(schema 2
shader "test.numeric" {
  properties { color tint = [1, 0, 0, 1] }
  pass "Forward" { fragment fragmentMain }
  slang { float4 fragmentMain() : SV_Target0 { return Material.tint; } }
}
)shader";
        const std::vector<std::uint8_t> sourceBytes(source.begin(), source.end());
        const auto authoring = cook(root, sourceBytes);
        const auto compiled = cook(root, sourceBytes, &authoring);
        const ShaderResourceReadRequest request{
            .guid = compiled.product.key.guid,
            .product = compiled.product,
            .artifactRoot = root,
            .targetProfile = "test-native",
            .stableTypeId = "test.numeric",
            .passName = "Forward",
            .stage = "fragment",
            .shaderProfile = "glsl_450",
        };
        const auto loaded = take(readShaderResource(request));
        require(loaded.productKey == compiled.product.key &&
                    loaded.productHash == compiled.product.productHash &&
                    loaded.spirv.front() == 0x07230203U && loaded.reflection.stage == "fragment" &&
                    loaded.reflection.descriptorBindings.size() == 1,
                "cooked entry lost bytecode/reflection/identity");

        const auto otherAuthoring = cook(root / "relocated", sourceBytes);
        const auto otherCompiled = cook(root / "relocated", sourceBytes, &otherAuthoring);
        require(otherCompiled.product.productHash == compiled.product.productHash,
                "reflection embeds a machine/output-root path");

        auto wrong = request;
        wrong.guid = take(parseAssetGuid("11111111-1111-1111-1111-111111111111"));
        expectShaderError(readShaderResource(wrong), ShaderResourceError::IdentityMismatch);
        wrong = request;
        wrong.product.key.assetType = makeAssetTypeId("Texture2D");
        expectShaderError(readShaderResource(wrong), ShaderResourceError::IdentityMismatch);
        wrong = request;
        wrong.targetProfile = "other-target";
        expectShaderError(readShaderResource(wrong), ShaderResourceError::IdentityMismatch);
        wrong = request;
        wrong.product.key.sourceHash ^= 1U;
        expectShaderError(readShaderResource(wrong), ShaderResourceError::IdentityMismatch);
        wrong = request;
        wrong.stableTypeId = "other.shader";
        expectShaderError(readShaderResource(wrong), ShaderResourceError::IdentityMismatch);
        wrong = request;
        wrong.passName = "Missing";
        expectShaderError(readShaderResource(wrong), ShaderResourceError::EntryUnavailable);
        wrong = request;
        wrong.stage = "vertex";
        expectShaderError(readShaderResource(wrong), ShaderResourceError::EntryUnavailable);
        wrong = request;
        wrong.product.productHash ^= 1U;
        require(!readShaderResource(wrong), "corrupt product hash accepted");
        wrong = request;
        wrong.product.relativeProductPath = "../outside.product";
        require(!readShaderResource(wrong), "path escape accepted");
        wrong = request;
        wrong.product.relativeProductPath = "missing.product";
        require(!readShaderResource(wrong), "missing product accepted");
        require(!readShaderResource(request, {.maxProductBytes = 1}), "product budget ignored");
        expectShaderError(readShaderResource(request, {.maxSpirvBytes = 1}),
                          ShaderResourceError::BudgetExceeded);
        expectShaderError(readShaderResource(request, {.maxReflectionBytes = 1}),
                          ShaderResourceError::BudgetExceeded);
        require(!parseShaderReflectionJson("{}"), "incomplete reflection accepted");

        // Rehash a tampered fixture to exercise semantic checks beyond artifact integrity.
        const auto original = take(
            core::readFileText(compiled.productFilePath, {.maxBytes = 64ULL * 1024ULL * 1024ULL}));
        auto readTampered = [&](const std::string& text) {
            const auto written = core::writeFileTextAtomically(compiled.productFilePath, text);
            require(written.has_value(), "failed to write tampered fixture");
            auto changed = request;
            changed.product.productSizeBytes = text.size();
            changed.product.productHash = hashAssetArtifactBytesV1(std::as_bytes(std::span{text}));
            return readShaderResource(changed);
        };
        auto tamper = [&](std::string_view from, std::string_view replacement) {
            std::string text = original;
            const auto offset = text.find(from);
            require(offset != std::string::npos, "missing tamper field");
            text.replace(offset, from.size(), replacement);
            return readTampered(text);
        };
        std::string duplicate = original;
        auto entryStart = original.find("entry.0.passName=");
        require(entryStart != std::string::npos, "entry fixture missing");
        std::string entryCopy = original.substr(entryStart);
        for (std::size_t offset = 0;
             (offset = entryCopy.find("entry.0.", offset)) != std::string::npos;) {
            entryCopy.replace(offset, 8, "entry.1.");
            offset += 8;
        }
        const auto countOffset = duplicate.find("entry.count=1");
        require(countOffset != std::string::npos, "entry count missing");
        duplicate.replace(countOffset, 13, "entry.count=2");
        duplicate += entryCopy;
        expectShaderError(readTampered(duplicate), ShaderResourceError::EntryUnavailable);
        require(
            !tamper("shader-compile-reflection-product.v2", "shader-compile-reflection-product.v1"),
            "legacy raw reflection product accepted");
        expectShaderError(tamper("slangcExitCode=0", "slangcExitCode=1"),
                          ShaderResourceError::InvalidCompiledEntry);
        expectShaderError(tamper("spirvValExitCode=0", "spirvValExitCode=1"),
                          ShaderResourceError::InvalidCompiledEntry);
        expectShaderError(tamper("compileEntry=fragmentMain", "compileEntry=otherEntry"),
                          ShaderResourceError::InvalidCompiledEntry);
        require(core::writeFileTextAtomically(compiled.productFilePath, original).has_value(),
                "failed to restore cooked fixture");
        const auto again = take(readShaderResource(request));
        require(again.spirv == loaded.spirv, "failed reads changed previously loaded data");
        std::puts("Cooked Shader runtime read passed: real Slang/SPIR-V product, identity, budgets "
                  "and failure checks.");
    }
} // namespace

int main() try {
    run();
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::fputs(error.what(), stderr);
    return EXIT_FAILURE;
} catch (...) {
    return EXIT_FAILURE;
}

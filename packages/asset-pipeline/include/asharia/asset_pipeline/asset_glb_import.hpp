#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/core/result.hpp"
#include "asharia/mesh_product/mesh_product_writer_v1.hpp"

namespace asharia::asset {

    inline constexpr std::string_view kGlbMeshImporterName = "com.asharia.importer.mesh.glb-static";
    inline constexpr std::string_view kGlbMeshAssetTypeName = "com.asharia.asset.Mesh";
    inline constexpr std::string_view kGlbMeshSourceExtension = ".glb";
    inline constexpr ImporterVersion kGlbMeshImporterVersion{1U};

    enum class AssetGlbImportDiagnosticCode {
        InvalidRequest = 1,
        UnsupportedSourceExtension,
        SourceByteLimitExceeded,
        InvalidGlb,
        JsonByteLimitExceeded,
        InvalidJson,
        ExternalUriUnsupported,
        RequiredExtensionUnsupported,
        UnsupportedBufferLayout,
        MissingDefaultScene,
        CountLimitExceeded,
        AnimationUnsupported,
        SceneSemanticUnsupported,
        SkinUnsupported,
        MorphTargetUnsupported,
        SparseAccessorUnsupported,
        UnsupportedPrimitiveTopology,
        UnsupportedVertexAttribute,
        MissingPosition,
        InvalidAccessor,
        InvalidIndex,
        NonFiniteValue,
        NonInvertibleTransform,
        DegenerateTriangle,
        EmptyMesh,
    };

    struct AssetGlbImporterDescriptor {
        std::string importerName;
        ImporterVersion importerVersion{};
        std::string supportedSourceExtension;

        [[nodiscard]] friend bool operator==(const AssetGlbImporterDescriptor&,
                                             const AssetGlbImporterDescriptor&) = default;
    };

    struct AssetGlbImportLimits {
        std::uint64_t maxSourceBytes{256ULL * 1024ULL * 1024ULL};
        std::uint64_t maxJsonBytes{16ULL * 1024ULL * 1024ULL};
        std::uint32_t maxJsonNestingDepth{256U};
        std::uint32_t maxNodes{65'536U};
        std::uint32_t maxNodeDepth{256U};
        std::uint32_t maxMeshes{65'536U};
        std::uint32_t maxPrimitives{65'536U};
        std::uint32_t maxMaterialSlots{65'536U};
        std::uint32_t maxVertices{8U * 1024U * 1024U};
        std::uint32_t maxIndices{24U * 1024U * 1024U};
        std::uint64_t maxDecodedBytes{1ULL * 1024ULL * 1024ULL * 1024ULL};

        [[nodiscard]] friend bool operator==(AssetGlbImportLimits, AssetGlbImportLimits) = default;
    };

    struct AssetGlbImportRequest {
        SourceAssetRecord source;
        std::vector<AssetImportSetting> settings;
        std::vector<std::uint8_t> sourceBytes;
        AssetGlbImportLimits limits;

        [[nodiscard]] friend bool operator==(const AssetGlbImportRequest&,
                                             const AssetGlbImportRequest&) = default;
    };

    [[nodiscard]] AssetGlbImporterDescriptor makeRestrictedGlbMeshImporterDescriptor();

    [[nodiscard]] bool isRestrictedGlbMeshImportCandidate(const SourceAssetRecord& source) noexcept;

    [[nodiscard]] const char*
    assetGlbImportDiagnosticCodeName(AssetGlbImportDiagnosticCode code) noexcept;

    [[nodiscard]] Result<mesh::MeshProductBuildInputV1>
    importRestrictedGlbMesh(const AssetGlbImportRequest& request);

} // namespace asharia::asset

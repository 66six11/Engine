#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "asharia/asset_pipeline/asset_product_execution.hpp"
#include "asharia/core/file_io.hpp"

namespace asharia::asset::detail {

    struct AssetProductPublicationItem {
        SourceAssetRecord source;
        AssetProductRecord product;
        std::filesystem::path finalPath;
        std::vector<std::uint8_t> bytes;
    };

    struct AssetProductPublicationRequest {
        std::filesystem::path outputRoot;
        std::filesystem::path manifestPath;
        AssetProductManifestDocument manifest;
        std::span<const AssetProductPublicationItem> products;
    };

    struct AssetProductPublicationResult {
        std::vector<AssetProductWrite> writes;
        bool manifestWritten{};
        std::optional<std::size_t> failingProductIndex;
    };

    class AssetProductPublicationOperations {
    public:
        virtual ~AssetProductPublicationOperations() = default;

        [[nodiscard]] virtual Result<std::filesystem::path>
        createUniqueStagingDirectory(const std::filesystem::path& outputRoot) = 0;

        [[nodiscard]] virtual VoidResult writeFileAtomically(const std::filesystem::path& path,
                                                             std::span<const std::byte> bytes) = 0;

        [[nodiscard]] virtual Result<std::vector<std::byte>>
        readFileBytes(const std::filesystem::path& path, core::FileReadLimits limits) = 0;

        [[nodiscard]] virtual VoidResult
        publishFileAtomically(const std::filesystem::path& stagingPath,
                              const std::filesystem::path& finalPath,
                              std::span<const std::byte> verifiedBytes) = 0;

        [[nodiscard]] virtual VoidResult
        removeStagingDirectory(const std::filesystem::path& stagingPath) = 0;
    };

    [[nodiscard]] VoidResult publishAssetProducts(const AssetProductPublicationRequest& request,
                                                  AssetProductPublicationOperations& operations,
                                                  AssetProductPublicationResult& outcome);

    [[nodiscard]] AssetProductExecutionResult
    executeAssetProductsWithPublicationOperations(const AssetProductExecutionRequest& request,
                                                  AssetProductPublicationOperations& operations);

    [[nodiscard]] AssetProductPublicationOperations& assetProductPublicationOperations();

} // namespace asharia::asset::detail

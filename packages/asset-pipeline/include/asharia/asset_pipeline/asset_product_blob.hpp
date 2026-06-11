#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::asset {

    enum class AssetProductBlobDiagnosticCode {
        MissingProduct,
        ProductReadFailed,
        InvalidProductBlob,
        MissingPayload,
        UnterminatedPayload,
    };

    struct AssetProductBlobReadRequest {
        std::filesystem::path productFilePath;
        std::string relativeProductPath;

        [[nodiscard]] friend bool operator==(const AssetProductBlobReadRequest&,
                                             const AssetProductBlobReadRequest&) = default;
    };

    struct AssetProductBlobPayload {
        std::vector<std::uint8_t> sourceBytes;

        [[nodiscard]] friend bool operator==(const AssetProductBlobPayload&,
                                             const AssetProductBlobPayload&) = default;
    };

    [[nodiscard]] const char*
    assetProductBlobDiagnosticCodeName(AssetProductBlobDiagnosticCode code) noexcept;

    [[nodiscard]] Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath);

} // namespace asharia::asset

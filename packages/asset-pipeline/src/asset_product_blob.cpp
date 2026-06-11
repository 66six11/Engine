#include "asharia/asset_pipeline/asset_product_blob.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::asset {
    namespace {

        [[nodiscard]] Error blobError(AssetProductBlobDiagnosticCode code,
                                      std::string relativeProductPath, std::string message) {
            const std::string product = relativeProductPath.empty()
                                            ? std::string{"<unspecified-product>"}
                                            : std::move(relativeProductPath);
            return Error{ErrorDomain::Asset, static_cast<int>(code),
                         "Asset product blob " + product + " " + std::move(message) + "."};
        }

    } // namespace

    Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(const AssetProductBlobReadRequest& request) {
        if (request.productFilePath.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingProduct,
                                             request.relativeProductPath,
                                             "has no product file path")};
        }

        std::ifstream file(request.productFilePath, std::ios::binary);
        if (!file) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingProduct,
                                             request.relativeProductPath,
                                             "is missing from the product cache")};
        }

        std::vector<std::uint8_t> bytes;
        char byte{};
        while (file.get(byte)) {
            bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
        if (!file.eof()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::ProductReadFailed,
                                             request.relativeProductPath,
                                             "could not be read completely")};
        }

        return readPlaceholderProductSourceBytes(
            std::span<const std::uint8_t>{bytes.data(), bytes.size()}, request.relativeProductPath);
    }

    Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath) {
        if (productBytes.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath}, "is empty")};
        }

        std::string text;
        text.reserve(productBytes.size());
        for (const std::uint8_t byte : productBytes) {
            text.push_back(static_cast<char>(byte));
        }
        constexpr std::string_view kBegin = "sourceBytes.begin\n";
        constexpr std::string_view kEnd = "\nsourceBytes.end";

        const std::size_t begin = text.find(kBegin);
        if (begin == std::string_view::npos) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingPayload,
                                             std::string{relativeProductPath},
                                             "does not contain sourceBytes.begin")};
        }

        const std::size_t payloadBegin = begin + kBegin.size();
        const std::size_t payloadEnd = text.find(kEnd, payloadBegin);
        if (payloadEnd == std::string_view::npos || payloadEnd < payloadBegin) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated sourceBytes payload")};
        }

        std::vector<std::uint8_t> sourceBytes;
        sourceBytes.reserve(payloadEnd - payloadBegin);
        for (std::size_t index = payloadBegin; index < payloadEnd; ++index) {
            sourceBytes.push_back(productBytes[index]);
        }

        return AssetProductBlobPayload{.sourceBytes = std::move(sourceBytes)};
    }

    const char* assetProductBlobDiagnosticCodeName(AssetProductBlobDiagnosticCode code) noexcept {
        switch (code) {
        case AssetProductBlobDiagnosticCode::MissingProduct:
            return "missing-product";
        case AssetProductBlobDiagnosticCode::ProductReadFailed:
            return "product-read-failed";
        case AssetProductBlobDiagnosticCode::InvalidProductBlob:
            return "invalid-product-blob";
        case AssetProductBlobDiagnosticCode::MissingPayload:
            return "missing-payload";
        case AssetProductBlobDiagnosticCode::UnterminatedPayload:
            return "unterminated-payload";
        }
        return "unknown";
    }

} // namespace asharia::asset

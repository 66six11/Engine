#include "asset_product_blob_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "asharia/asset_pipeline/asset_product_blob.hpp"
#include "asharia/core/error.hpp"

namespace asharia::asset::detail {
    namespace {

        [[nodiscard]] Error recordLimitError(const AssetProductRecordLimitRequest& request,
                                             std::string reason) {
            const std::string product = request.relativeProductPath.empty()
                                            ? std::string{"<unspecified-product>"}
                                            : std::string{request.relativeProductPath};
            return Error{ErrorDomain::Asset,
                         static_cast<int>(AssetProductBlobDiagnosticCode::InvalidProductBlob),
                         "Asset product blob " + product + " has " +
                             std::string{request.recordName} + " " + std::move(reason) + "."};
        }

    } // namespace

    Result<std::size_t>
    validateAssetProductRecordCount(const AssetProductRecordLimitRequest& request) {
        if (request.minimumLinesPerRecord == 0U) {
            return std::unexpected{recordLimitError(request, "with no minimum field count")};
        }
        if (request.count > request.hardLimit) {
            return std::unexpected{recordLimitError(
                request, "count " + std::to_string(request.count) + " exceeding configured limit " +
                             std::to_string(request.hardLimit))};
        }
        if (request.count > static_cast<std::uint64_t>(SIZE_MAX)) {
            return std::unexpected{
                recordLimitError(request, "count that does not fit the host address space")};
        }
        if (request.count > request.headerLineCount / request.minimumLinesPerRecord) {
            return std::unexpected{recordLimitError(
                request, "count unsupported by the available product header fields")};
        }

        return static_cast<std::size_t>(request.count);
    }

} // namespace asharia::asset::detail

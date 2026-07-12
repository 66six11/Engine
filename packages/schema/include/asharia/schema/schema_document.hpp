#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/schema/type_schema.hpp"

namespace asharia::schema {

    inline constexpr std::uint64_t kMaxSchemaDocumentBytes = 64ULL * 1024ULL * 1024ULL;

    struct SchemaDocumentFileOptions {
        std::uint64_t maxBytes{kMaxSchemaDocumentBytes};
    };

    [[nodiscard]] Result<std::vector<TypeSchema>> readSchemaDocument(std::string_view text);
    [[nodiscard]] Result<std::vector<TypeSchema>>
    readSchemaDocumentFile(const std::filesystem::path& path,
                           SchemaDocumentFileOptions options = {});

} // namespace asharia::schema

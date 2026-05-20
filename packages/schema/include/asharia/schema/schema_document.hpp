#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/schema/type_schema.hpp"

namespace asharia::schema {

    [[nodiscard]] Result<std::vector<TypeSchema>> readSchemaDocument(std::string_view text);
    [[nodiscard]] Result<std::vector<TypeSchema>>
    readSchemaDocumentFile(const std::filesystem::path& path);

} // namespace asharia::schema

#include "asset_processor_text.hpp"

#include <cstddef>

namespace asharia::asset_processor {

    std::string pathText(const std::filesystem::path& path) {
        return path.generic_string();
    }

    std::string escaped(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (const char character : text) {
            switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += character;
                break;
            }
        }
        return result;
    }

    std::string quoteText(std::string_view text) {
        return "\"" + escaped(text) + "\"";
    }

    std::string quotePath(const std::filesystem::path& path) {
        return quoteText(pathText(path));
    }

    std::string formatHash64(std::uint64_t value) {
        constexpr std::string_view kHexDigits = "0123456789abcdef";
        std::string text(16, '0');
        for (std::size_t index = 0; index < text.size(); ++index) {
            const auto shift = static_cast<std::uint32_t>((text.size() - index - 1) * 4);
            text[index] = kHexDigits[(value >> shift) & 0xFU];
        }
        return text;
    }

} // namespace asharia::asset_processor

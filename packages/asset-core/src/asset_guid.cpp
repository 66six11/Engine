#include "asharia/asset_core/asset_guid.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <string>

namespace asharia::asset {
    namespace {

        constexpr std::size_t kUuidTextLength = 36;
        constexpr std::array<std::size_t, 4> kHyphenPositions{8, 13, 18, 23};

        [[nodiscard]] bool isHyphenPosition(std::size_t index) noexcept {
            return std::ranges::find(kHyphenPositions, index) != kHyphenPositions.end();
        }

        [[nodiscard]] int hexValue(char character) noexcept {
            if (character >= '0' && character <= '9') {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f') {
                return 10 + character - 'a';
            }
            if (character >= 'A' && character <= 'F') {
                return 10 + character - 'A';
            }
            return -1;
        }

        [[nodiscard]] Error assetGuidError(std::string_view text, std::string_view reason) {
            return Error{
                ErrorDomain::Asset,
                1,
                "Invalid asset GUID \"" + std::string{text} + "\": " + std::string{reason},
            };
        }

    } // namespace

    AssetGuid::operator bool() const noexcept {
        return std::ranges::any_of(bytes, [](std::uint8_t byte) { return byte != 0; });
    }

    Result<AssetGuid> parseAssetGuid(std::string_view text) {
        if (text.size() != kUuidTextLength) {
            return std::unexpected{assetGuidError(text, "expected 36 characters")};
        }

        AssetGuid guid{};
        auto byte = guid.bytes.begin();
        bool highNibble = true;
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char character = text[index];
            if (isHyphenPosition(index)) {
                if (character != '-') {
                    return std::unexpected{assetGuidError(text, "expected hyphen separators")};
                }
                continue;
            }

            const int value = hexValue(character);
            if (value < 0) {
                return std::unexpected{assetGuidError(text, "expected hexadecimal digits")};
            }

            if (highNibble) {
                *byte = static_cast<std::uint8_t>(value << 4);
            } else {
                *byte = static_cast<std::uint8_t>(*byte | value);
                ++byte;
            }
            highNibble = !highNibble;
        }

        if (!guid) {
            return std::unexpected{assetGuidError(text, "zero GUID is invalid")};
        }

        return guid;
    }

    std::string formatAssetGuid(AssetGuid guid) {
        constexpr std::string_view kHexDigits = "0123456789abcdef";

        std::string text;
        text.reserve(kUuidTextLength);
        std::size_t byteIndex = 0;
        for (const std::uint8_t byte : guid.bytes) {
            if (byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10) {
                text.push_back('-');
            }

            text.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
            text.push_back(kHexDigits[byte & 0x0FU]);
            ++byteIndex;
        }

        return text;
    }

} // namespace asharia::asset

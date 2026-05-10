#include "vke/serialization/text_archive.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace vke::serialization {
    namespace {

        using OrderedJson = nlohmann::ordered_json;

        [[nodiscard]] Error textArchiveError(std::string message) {
            return Error{ErrorDomain::Serialization, 0, std::move(message)};
        }

        struct DuplicateKeyError final : std::runtime_error {
            explicit DuplicateKeyError(const std::string& key)
                : std::runtime_error{"JSON object contains duplicate key: " + key} {}
        };

        [[nodiscard]] OrderedJson toJson(const ArchiveValue& value) {
            switch (value.kind) {
            case ArchiveValueKind::Null:
                return nullptr;
            case ArchiveValueKind::Bool:
                return value.boolValue;
            case ArchiveValueKind::Integer:
                return value.integerValue;
            case ArchiveValueKind::Float:
                if (!std::isfinite(value.floatValue)) {
                    throw std::domain_error{
                        "JSON archive cannot represent non-finite float values."};
                }
                return value.floatValue;
            case ArchiveValueKind::String:
                return value.stringValue;
            case ArchiveValueKind::Array: {
                OrderedJson array = OrderedJson::array();
                for (const ArchiveValue& element : value.arrayValue) {
                    array.push_back(toJson(element));
                }
                return array;
            }
            case ArchiveValueKind::Object: {
                OrderedJson object = OrderedJson::object();
                for (const ArchiveMember& member : value.objectValue) {
                    if (object.contains(member.key)) {
                        throw DuplicateKeyError{member.key};
                    }
                    object[member.key] = toJson(member.value);
                }
                return object;
            }
            }
            return nullptr;
        }

        [[nodiscard]] Result<ArchiveValue> fromJson(const OrderedJson& json) {
            if (json.is_null()) {
                return ArchiveValue::null();
            }
            if (json.is_boolean()) {
                return ArchiveValue::boolean(json.get<bool>());
            }
            if (json.is_number_integer()) {
                return ArchiveValue::integer(json.get<std::int64_t>());
            }
            if (json.is_number_unsigned()) {
                const auto value = json.get<std::uint64_t>();
                if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    return std::unexpected{textArchiveError(
                        "JSON unsigned integer is outside the archive integer range.")};
                }
                return ArchiveValue::integer(static_cast<std::int64_t>(value));
            }
            if (json.is_number_float()) {
                return ArchiveValue::floating(json.get<double>());
            }
            if (json.is_string()) {
                return ArchiveValue::string(json.get<std::string>());
            }
            if (json.is_array()) {
                std::vector<ArchiveValue> values;
                values.reserve(json.size());
                for (const OrderedJson& element : json) {
                    auto value = fromJson(element);
                    if (!value) {
                        return std::unexpected{std::move(value.error())};
                    }
                    values.push_back(std::move(*value));
                }
                return ArchiveValue::array(std::move(values));
            }
            if (json.is_object()) {
                std::vector<ArchiveMember> members;
                members.reserve(json.size());
                for (auto iterator = json.begin(); iterator != json.end(); ++iterator) {
                    auto value = fromJson(iterator.value());
                    if (!value) {
                        return std::unexpected{std::move(value.error())};
                    }
                    members.push_back(ArchiveMember{
                        .key = iterator.key(),
                        .value = std::move(*value),
                    });
                }
                return ArchiveValue::object(std::move(members));
            }

            return std::unexpected{textArchiveError("Unsupported JSON value in text archive.")};
        }

        [[nodiscard]] OrderedJson parseStrictJson(std::string_view text) {
            std::vector<std::vector<std::string>> objectKeyStack;
            const OrderedJson::parser_callback_t rejectDuplicateKeys =
                [&objectKeyStack](int, OrderedJson::parse_event_t event,
                                  OrderedJson& parsed) -> bool {
                switch (event) {
                case OrderedJson::parse_event_t::object_start:
                    objectKeyStack.emplace_back();
                    break;
                case OrderedJson::parse_event_t::object_end:
                    if (!objectKeyStack.empty()) {
                        objectKeyStack.pop_back();
                    }
                    break;
                case OrderedJson::parse_event_t::key: {
                    if (objectKeyStack.empty()) {
                        break;
                    }
                    const std::string key = parsed.get<std::string>();
                    std::vector<std::string>& keys = objectKeyStack.back();
                    if (std::ranges::find(keys, key) != keys.end()) {
                        throw DuplicateKeyError{key};
                    }
                    keys.push_back(key);
                    break;
                }
                default:
                    break;
                }
                return true;
            };

            return OrderedJson::parse(text, rejectDuplicateKeys, true, false);
        }

    } // namespace

    Result<std::string> writeTextArchive(const ArchiveValue& value) {
        try {
            std::string output =
                toJson(value).dump(2, ' ', false, OrderedJson::error_handler_t::strict);
            output += '\n';
            return output;
        } catch (const DuplicateKeyError& exception) {
            return std::unexpected{textArchiveError(exception.what())};
        } catch (const nlohmann::json::exception& exception) {
            return std::unexpected{textArchiveError("Failed to write text archive JSON: " +
                                                    std::string{exception.what()})};
        } catch (const std::exception& exception) {
            return std::unexpected{textArchiveError("Failed to write text archive JSON: " +
                                                    std::string{exception.what()})};
        }
    }

    Result<ArchiveValue> readTextArchive(std::string_view text) {
        try {
            const OrderedJson parsed = parseStrictJson(text);
            return fromJson(parsed);
        } catch (const DuplicateKeyError& exception) {
            return std::unexpected{textArchiveError(exception.what())};
        } catch (const nlohmann::json::parse_error& exception) {
            return std::unexpected{textArchiveError("Failed to parse text archive JSON at byte " +
                                                    std::to_string(exception.byte) + ": " +
                                                    exception.what())};
        } catch (const nlohmann::json::exception& exception) {
            return std::unexpected{textArchiveError("Failed to read text archive JSON: " +
                                                    std::string{exception.what()})};
        }
    }

    Result<ArchiveValue> readTextArchiveFile(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) {
            return std::unexpected{
                textArchiveError("Failed to open text archive file: " + path.string())};
        }

        const std::streamsize size = file.tellg();
        if (size < 0) {
            return std::unexpected{
                textArchiveError("Failed to measure text archive file: " + path.string())};
        }

        std::string text(static_cast<std::size_t>(size), '\0');
        file.seekg(0, std::ios::beg);
        if (!file.read(text.data(), size)) {
            return std::unexpected{
                textArchiveError("Failed to read text archive file: " + path.string())};
        }

        auto archive = readTextArchive(text);
        if (!archive) {
            return std::unexpected{textArchiveError("Failed to parse text archive file '" +
                                                    path.string() +
                                                    "': " + archive.error().message)};
        }
        return archive;
    }

} // namespace vke::serialization

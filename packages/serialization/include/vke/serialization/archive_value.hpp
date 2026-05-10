#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vke::serialization {

    enum class ArchiveValueKind {
        Null,
        Bool,
        Integer,
        Float,
        String,
        Array,
        Object,
    };

    struct ArchiveMember;

    struct ArchiveValue {
        ArchiveValue();
        ArchiveValue(const ArchiveValue& other);
        ArchiveValue(ArchiveValue&& other) noexcept;
        ArchiveValue& operator=(const ArchiveValue& other);
        ArchiveValue& operator=(ArchiveValue&& other) noexcept;
        ~ArchiveValue();

        [[nodiscard]] static ArchiveValue null();
        [[nodiscard]] static ArchiveValue boolean(bool value);
        [[nodiscard]] static ArchiveValue integer(std::int64_t value);
        [[nodiscard]] static ArchiveValue floating(double value);
        [[nodiscard]] static ArchiveValue string(std::string value);
        [[nodiscard]] static ArchiveValue array(std::vector<ArchiveValue> values);
        [[nodiscard]] static ArchiveValue object(std::vector<ArchiveMember> members);

        [[nodiscard]] const ArchiveMember* findMember(std::string_view key) const;
        [[nodiscard]] ArchiveMember* findMember(std::string_view key);
        [[nodiscard]] const ArchiveValue* findMemberValue(std::string_view key) const;
        [[nodiscard]] ArchiveValue* findMemberValue(std::string_view key);

        ArchiveValueKind kind{ArchiveValueKind::Null};
        bool boolValue{};
        std::int64_t integerValue{};
        double floatValue{};
        std::string stringValue;
        std::vector<ArchiveValue> arrayValue;
        std::vector<ArchiveMember> objectValue;
    };

    struct ArchiveMember {
        std::string key;
        ArchiveValue value;
    };

} // namespace vke::serialization

#include "vke/serialization/archive_value.hpp"

#include <algorithm>
#include <utility>

namespace vke::serialization {

    ArchiveValue::ArchiveValue() = default;
    ArchiveValue::ArchiveValue(const ArchiveValue& other) = default;
    ArchiveValue::ArchiveValue(ArchiveValue&& other) noexcept = default;
    ArchiveValue& ArchiveValue::operator=(const ArchiveValue& other) = default;
    ArchiveValue& ArchiveValue::operator=(ArchiveValue&& other) noexcept = default;
    ArchiveValue::~ArchiveValue() = default;

    ArchiveValue ArchiveValue::null() {
        return ArchiveValue{};
    }

    ArchiveValue ArchiveValue::boolean(bool value) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::Bool;
        archive.boolValue = value;
        return archive;
    }

    ArchiveValue ArchiveValue::integer(std::int64_t value) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::Integer;
        archive.integerValue = value;
        return archive;
    }

    ArchiveValue ArchiveValue::floating(double value) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::Float;
        archive.floatValue = value;
        return archive;
    }

    ArchiveValue ArchiveValue::string(std::string value) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::String;
        archive.stringValue = std::move(value);
        return archive;
    }

    ArchiveValue ArchiveValue::array(std::vector<ArchiveValue> values) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::Array;
        archive.arrayValue = std::move(values);
        return archive;
    }

    ArchiveValue ArchiveValue::object(std::vector<ArchiveMember> members) {
        ArchiveValue archive;
        archive.kind = ArchiveValueKind::Object;
        archive.objectValue = std::move(members);
        return archive;
    }

    const ArchiveMember* ArchiveValue::findMember(std::string_view key) const {
        if (kind != ArchiveValueKind::Object) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            objectValue, [key](const ArchiveMember& member) { return member.key == key; });
        return found == objectValue.end() ? nullptr : &*found;
    }

    ArchiveMember* ArchiveValue::findMember(std::string_view key) {
        if (kind != ArchiveValueKind::Object) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            objectValue, [key](const ArchiveMember& member) { return member.key == key; });
        return found == objectValue.end() ? nullptr : &*found;
    }

    const ArchiveValue* ArchiveValue::findMemberValue(std::string_view key) const {
        const ArchiveMember* member = findMember(key);
        return member == nullptr ? nullptr : &member->value;
    }

    ArchiveValue* ArchiveValue::findMemberValue(std::string_view key) {
        ArchiveMember* member = findMember(key);
        return member == nullptr ? nullptr : &member->value;
    }

} // namespace vke::serialization

#include "asharia/host_runtime/static_factory_registration_snapshot_json.hpp"

#include <new>
#include <stdexcept>
#include <string_view>

namespace asharia::host_runtime {
    namespace {

        constexpr std::string_view kSchema{"com.asharia.static-factory-registration-snapshot"};

        [[nodiscard]] constexpr char hexDigit(unsigned char nibble) noexcept {
            return nibble < 10U ? static_cast<char>('0' + nibble)
                                : static_cast<char>('a' + (nibble - 10U));
        }

        void appendJsonString(std::string& output, std::string_view value) {
            output.push_back('"');
            for (const char character : value) {
                const auto byte = static_cast<unsigned char>(character);
                switch (byte) {
                case '"':
                    output += "\\\"";
                    break;
                case '\\':
                    output += "\\\\";
                    break;
                case '\b':
                    output += "\\b";
                    break;
                case '\f':
                    output += "\\f";
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                default:
                    if (byte < 0x20U) {
                        // Other JSON control bytes use the deterministic lowercase form.
                        output += "\\u00";
                        output.push_back(hexDigit((byte >> 4U) & 0x0FU));
                        output.push_back(hexDigit(byte & 0x0FU));
                    } else {
                        output.push_back(character);
                    }
                    break;
                }
            }
            output.push_back('"');
        }

        void appendRegistration(std::string& output,
                                const StaticFactoryRegistrationV1& registration) {
            output += "    {\n      \"packageId\": ";
            appendJsonString(output, registration.packageId);
            output += ",\n      \"packageVersion\": ";
            appendJsonString(output, registration.packageVersion);
            output += ",\n      \"moduleId\": ";
            appendJsonString(output, registration.moduleId);
            output += ",\n      \"factoryId\": ";
            appendJsonString(output, registration.factoryId);
            output += ",\n      \"providerEntryPoint\": ";
            appendJsonString(output, registration.providerEntryPoint);
            output += "\n    }";
        }

    } // namespace

    std::expected<std::string, StaticFactoryRegistrationSnapshotJsonRenderError>
    renderStaticFactoryRegistrationSnapshotJson(
        const StaticFactoryRegistrationSnapshotV1& snapshot) noexcept {
        try {
            std::string output{"{\n  \"schema\": "};
            appendJsonString(output, kSchema);
            output += ",\n  \"schemaVersion\": 1,\n  \"generationId\": ";
            appendJsonString(output, snapshot.generationId);
            output += ",\n  \"hostActivationBlueprintSha256\": ";
            appendJsonString(output, snapshot.hostActivationBlueprintSha256);
            output += ",\n  \"registrations\": [";
            if (!snapshot.registrations.empty()) {
                output.push_back('\n');
                for (std::size_t index = 0; index < snapshot.registrations.size(); ++index) {
                    appendRegistration(output, snapshot.registrations[index]);
                    output += index + 1U == snapshot.registrations.size() ? "\n" : ",\n";
                }
                output += "  ]\n}\n";
            } else {
                output += "]\n}\n";
            }
            return output;
        } catch (const std::bad_alloc&) {
            return std::unexpected(
                StaticFactoryRegistrationSnapshotJsonRenderError::AllocationFailed);
        } catch (const std::length_error&) {
            return std::unexpected(
                StaticFactoryRegistrationSnapshotJsonRenderError::AllocationFailed);
        }
    }

} // namespace asharia::host_runtime

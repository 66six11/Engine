#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace asharia::sample_viewer {
namespace {

    constexpr std::string_view kScriptLabHostKindCppClr = "cppClr";
    constexpr std::string_view kManifestHostfxrPath = "hostfxrPath";
    constexpr std::string_view kManifestRuntimeConfigPath = "runtimeConfigPath";
    constexpr std::string_view kManifestAssemblyPath = "assemblyPath";
    constexpr std::string_view kManifestTypeName = "typeName";
    constexpr std::string_view kManifestPrepareMethod = "prepareMethod";
    constexpr std::string_view kManifestEntryMethod = "entryMethod";

    enum class ScriptLabHostExitCode : std::uint8_t {
        Ok = 0,
        InvalidCommandLine = 2,
        HostfxrLoadFailed = 3,
        HostfxrExportsMissing = 4,
        RuntimeConfigInitializationFailed = 5,
        LoadAssemblyDelegateUnavailable = 6,
        PrepareMethodUnresolved = 7,
        PrepareMethodFailed = 8,
        EntryMethodUnresolved = 9,
        BridgeManifestInvalid = 10,
        UnsupportedPlatform = 11,
    };

    std::optional<std::string_view> argValue(std::span<char*> args, std::string_view option) {
        for (std::size_t index = 1; index + 1 < args.size(); ++index) {
            if (args[index] != nullptr && args[index] == option && args[index + 1] != nullptr) {
                return std::string_view{args[index + 1]};
            }
        }
        return std::nullopt;
    }

    void printScriptLabHostUsage() {
        std::cerr << "Usage: asharia-sample-viewer --scriptlab-host "
                     "--scriptlab-bridge-manifest path "
                     "--scriptlab-ready-file path "
                     "--scriptlab-go-file path\n";
    }

#if defined(_WIN32)

    enum hostfxr_delegate_type { // NOLINT(performance-enum-size): hostfxr ABI enum.
        hdt_com_activation,
        hdt_load_in_memory_assembly,
        hdt_winrt_activation,
        hdt_com_register,
        hdt_com_unregister,
        hdt_load_assembly_and_get_function_pointer
    };

    using hostfxr_handle = void*;
    using hostfxr_initialize_for_runtime_config_fn =
        std::int32_t(__cdecl*)(const wchar_t*, const void*, hostfxr_handle*);
    using hostfxr_get_runtime_delegate_fn =
        std::int32_t(__cdecl*)(const hostfxr_handle, hostfxr_delegate_type, void**);
    using hostfxr_close_fn = std::int32_t(__cdecl*)(const hostfxr_handle);
    using load_assembly_and_get_function_pointer_fn =
        int(__stdcall*)(const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, void*, void**);
    using bridge_component_entry_point_fn = int(__stdcall*)(void*, std::int32_t);

    struct ScriptLabHostConfiguration {
        std::wstring hostfxrPath;
        std::wstring runtimeConfigPath;
        std::wstring bridgeAssemblyPath;
        std::wstring typeName;
        std::wstring prepareMethod;
        std::wstring entryMethod;
        std::filesystem::path readyPath;
        std::filesystem::path goPath;
    };

    struct ScriptLabHostPaths {
        std::filesystem::path manifestPath;
        std::filesystem::path readyPath;
        std::filesystem::path goPath;
    };

    template <typename FunctionT, typename PointerT>
    FunctionT bitCastFunction(PointerT pointer) {
        static_assert(sizeof(FunctionT) == sizeof(PointerT));
        return std::bit_cast<FunctionT>(pointer);
    }

    bool fileExists(const std::filesystem::path& path) {
        return GetFileAttributesW(path.wstring().c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool isJsonSpace(char value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }

    int hexValue(char value) {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    }

    bool parseHexQuad(const std::string& text, std::size_t index, std::uint32_t& codePoint) {
        std::uint32_t value = 0;
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const int digit = index + offset < text.size() ? hexValue(text[index + offset]) : -1;
            if (digit < 0) {
                return false;
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }

        codePoint = value;
        return true;
    }

    void appendUtf8(std::uint32_t codePoint, std::string& output) {
        if (codePoint <= 0x7f) {
            output.push_back(static_cast<char>(codePoint));
            return;
        }
        if (codePoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            return;
        }
        if (codePoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            return;
        }

        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    bool parseJsonString(const std::string& text, std::size_t& index, std::string& value) {
        if (index >= text.size() || text[index] != '"') {
            return false;
        }

        ++index;
        while (index < text.size()) {
            const char next = text[index++];
            if (next == '"') {
                return true;
            }
            if (next != '\\') {
                value.push_back(next);
                continue;
            }
            if (index >= text.size()) {
                return false;
            }

            const char escape = text[index++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escape);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                std::uint32_t codePoint = 0;
                if (!parseHexQuad(text, index, codePoint)) {
                    return false;
                }

                index += 4;
                if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
                    if (index + 6 > text.size() || text[index] != '\\' ||
                        text[index + 1] != 'u') {
                        return false;
                    }

                    std::uint32_t lowSurrogate = 0;
                    if (!parseHexQuad(text, index + 2, lowSurrogate) ||
                        lowSurrogate < 0xdc00 || lowSurrogate > 0xdfff) {
                        return false;
                    }

                    index += 6;
                    codePoint =
                        0x10000 + (((codePoint - 0xd800) << 10) | (lowSurrogate - 0xdc00));
                }

                appendUtf8(codePoint, value);
                break;
            }
            default:
                return false;
            }
        }

        return false;
    }

    bool findJsonStringValue(const std::string& text,
                             std::string_view propertyName,
                             std::string& value) {
        const std::string quotedName = "\"" + std::string{propertyName} + "\"";
        const std::size_t nameIndex = text.find(quotedName);
        if (nameIndex == std::string::npos) {
            return false;
        }

        std::size_t index = nameIndex + quotedName.size();
        while (index < text.size() && isJsonSpace(text[index])) {
            ++index;
        }

        if (index >= text.size() || text[index] != ':') {
            return false;
        }

        ++index;
        while (index < text.size() && isJsonSpace(text[index])) {
            ++index;
        }

        return parseJsonString(text, index, value);
    }

    bool utf8ToWide(const std::string& value, std::wstring& wide) {
        if (value.empty()) {
            wide.clear();
            return true;
        }

        const int required = MultiByteToWideChar(CP_UTF8,
                                                 MB_ERR_INVALID_CHARS,
                                                 value.data(),
                                                 static_cast<int>(value.size()),
                                                 nullptr,
                                                 0);
        if (required <= 0) {
            return false;
        }

        wide.assign(static_cast<std::size_t>(required), L'\0');
        return MultiByteToWideChar(CP_UTF8,
                                   MB_ERR_INVALID_CHARS,
                                   value.data(),
                                   static_cast<int>(value.size()),
                                   wide.data(),
                                   required) == required;
    }

    bool readManifestField(const std::string& manifest,
                           std::string_view propertyName,
                           std::wstring& value) {
        std::string utf8Value;
        return findJsonStringValue(manifest, propertyName, utf8Value) &&
               utf8ToWide(utf8Value, value) && !value.empty();
    }

    bool readUtf8File(const std::filesystem::path& path, std::string& text) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) {
            return false;
        }

        const std::ifstream::pos_type size = file.tellg();
        const auto kMaxManifestBytes =
            static_cast<std::ifstream::pos_type>(static_cast<std::streamoff>(1024) * 1024);
        if (size < 0 || size > kMaxManifestBytes) {
            return false;
        }

        const auto byteCount = static_cast<std::size_t>(size);
        text.assign(byteCount, '\0');
        file.seekg(0);
        return text.empty() ||
               static_cast<bool>(file.read(text.data(), static_cast<std::streamsize>(byteCount)));
    }

    bool loadManifestConfiguration(const ScriptLabHostPaths& paths,
                                   ScriptLabHostConfiguration& configuration) {
        std::string manifest;
        if (!readUtf8File(paths.manifestPath, manifest)) {
            return false;
        }

        configuration.readyPath = paths.readyPath;
        configuration.goPath = paths.goPath;
        return readManifestField(manifest, kManifestHostfxrPath, configuration.hostfxrPath) &&
               readManifestField(
                   manifest, kManifestRuntimeConfigPath, configuration.runtimeConfigPath) &&
               readManifestField(manifest, kManifestAssemblyPath, configuration.bridgeAssemblyPath) &&
               readManifestField(manifest, kManifestTypeName, configuration.typeName) &&
               readManifestField(manifest, kManifestPrepareMethod, configuration.prepareMethod) &&
               readManifestField(manifest, kManifestEntryMethod, configuration.entryMethod);
    }

    bool writeReadyFile(const std::filesystem::path& path) {
        if (const std::filesystem::path parent = path.parent_path(); !parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
        }

        std::ofstream ready{path, std::ios::binary};
        ready << "ready";
        return static_cast<bool>(ready);
    }

    int loadComponentEntry(load_assembly_and_get_function_pointer_fn loadAssembly,
                           const std::wstring& bridgeAssemblyPath,
                           const std::wstring& typeName,
                           const std::wstring& methodName,
                           bridge_component_entry_point_fn& entryPoint) {
        void* entryPtr = nullptr;
        const int result = loadAssembly(bridgeAssemblyPath.c_str(),
                                        typeName.c_str(),
                                        methodName.c_str(),
                                        nullptr,
                                        nullptr,
                                        &entryPtr);
        if (result < 0 || entryPtr == nullptr) {
            return result < 0 ? result : -1;
        }

        entryPoint = bitCastFunction<bridge_component_entry_point_fn>(entryPtr);
        return 0;
    }

    int runWindowsScriptLabHost(const ScriptLabHostConfiguration& configuration) {
        const HMODULE hostfxr = LoadLibraryW(configuration.hostfxrPath.c_str());
        if (hostfxr == nullptr) {
            std::cerr << "Failed to load hostfxr from bridge manifest.\n";
            return static_cast<int>(ScriptLabHostExitCode::HostfxrLoadFailed);
        }

        auto initialize = bitCastFunction<hostfxr_initialize_for_runtime_config_fn>(
            GetProcAddress(hostfxr, "hostfxr_initialize_for_runtime_config"));
        auto getDelegate = bitCastFunction<hostfxr_get_runtime_delegate_fn>(
            GetProcAddress(hostfxr, "hostfxr_get_runtime_delegate"));
        auto close = bitCastFunction<hostfxr_close_fn>(GetProcAddress(hostfxr, "hostfxr_close"));
        if (initialize == nullptr || getDelegate == nullptr || close == nullptr) {
            std::cerr << "hostfxr exports are incomplete.\n";
            return static_cast<int>(ScriptLabHostExitCode::HostfxrExportsMissing);
        }

        hostfxr_handle context = nullptr;
        std::int32_t result =
            initialize(configuration.runtimeConfigPath.c_str(), nullptr, &context);
        if (result < 0 || context == nullptr) {
            std::cerr << "Failed to initialize CoreCLR from bridge runtimeconfig.\n";
            return static_cast<int>(ScriptLabHostExitCode::RuntimeConfigInitializationFailed);
        }

        void* loadAssemblyPtr = nullptr;
        result = getDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssemblyPtr);
        close(context);
        if (result < 0 || loadAssemblyPtr == nullptr) {
            std::cerr << "Failed to resolve CoreCLR assembly load delegate.\n";
            return static_cast<int>(ScriptLabHostExitCode::LoadAssemblyDelegateUnavailable);
        }

        auto loadAssembly =
            bitCastFunction<load_assembly_and_get_function_pointer_fn>(loadAssemblyPtr);
        bridge_component_entry_point_fn prepare = nullptr;
        result = loadComponentEntry(loadAssembly,
                                    configuration.bridgeAssemblyPath,
                                    configuration.typeName,
                                    configuration.prepareMethod,
                                    prepare);
        if (result != 0 || prepare == nullptr) {
            std::cerr << "Failed to resolve ScriptLab prepare method.\n";
            return static_cast<int>(ScriptLabHostExitCode::PrepareMethodUnresolved);
        }

        result = prepare(nullptr, 0);
        if (result != 0) {
            std::cerr << "ScriptLab prepare method failed with code " << result << ".\n";
            return static_cast<int>(ScriptLabHostExitCode::PrepareMethodFailed);
        }

        bridge_component_entry_point_fn entry = nullptr;
        result = loadComponentEntry(loadAssembly,
                                    configuration.bridgeAssemblyPath,
                                    configuration.typeName,
                                    configuration.entryMethod,
                                    entry);
        if (result != 0 || entry == nullptr) {
            std::cerr << "Failed to resolve ScriptLab entry method.\n";
            return static_cast<int>(ScriptLabHostExitCode::EntryMethodUnresolved);
        }

        if (!writeReadyFile(configuration.readyPath)) {
            std::cerr << "Failed to write ScriptLab ready file.\n";
            return static_cast<int>(ScriptLabHostExitCode::InvalidCommandLine);
        }

        std::cout << "ScriptLab host ready: " << configuration.readyPath.string() << '\n';
        while (!fileExists(configuration.goPath)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }

        std::cout << "ScriptLab host running entry method.\n";
        return entry(nullptr, 0);
    }

#endif

} // namespace

int runScriptLabHost(std::span<char*> args) {
    const std::optional<std::string_view> manifestArg =
        argValue(args, "--scriptlab-bridge-manifest");
    const std::optional<std::string_view> readyArg = argValue(args, "--scriptlab-ready-file");
    const std::optional<std::string_view> goArg = argValue(args, "--scriptlab-go-file");

    if (!manifestArg || !readyArg || !goArg) {
        printScriptLabHostUsage();
        return static_cast<int>(ScriptLabHostExitCode::InvalidCommandLine);
    }

#if defined(_WIN32)
    ScriptLabHostConfiguration configuration{};
    const ScriptLabHostPaths paths{
        .manifestPath = std::filesystem::path{std::string{*manifestArg}},
        .readyPath = std::filesystem::path{std::string{*readyArg}},
        .goPath = std::filesystem::path{std::string{*goArg}},
    };
    if (!loadManifestConfiguration(paths, configuration)) {
        std::cerr << "Failed to read ScriptLab bridge manifest.\n";
        return static_cast<int>(ScriptLabHostExitCode::BridgeManifestInvalid);
    }

    std::cout << "ScriptLab host kind: " << kScriptLabHostKindCppClr << '\n';
    return runWindowsScriptLabHost(configuration);
#else
    std::cerr << "ScriptLab host mode is currently implemented for Windows hostfxr only.\n";
    return static_cast<int>(ScriptLabHostExitCode::UnsupportedPlatform);
#endif
}

} // namespace asharia::sample_viewer

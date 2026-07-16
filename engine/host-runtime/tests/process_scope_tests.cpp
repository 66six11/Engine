#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string_view>

#include "process_scope_test_support.hpp"

int main(int argc, char** argv) {
    using asharia::host_runtime::tests::NamedProcessScopeTestV1;
    const std::span<char*> arguments{argv, static_cast<std::size_t>(argc)};
    if (arguments.size() == 2 && std::string_view{arguments[1]} == "--probe-active-scope-drop") {
        asharia::host_runtime::tests::runActiveProcessScopeDropProbe();
    }

    try {
        const std::array groups{
            asharia::host_runtime::tests::processScopeContractTests(),
            asharia::host_runtime::tests::processScopeContributionContractTests(),
            asharia::host_runtime::tests::processScopeContributionLifecycleTests(),
            asharia::host_runtime::tests::processScopeZeroContributionTests(),
            asharia::host_runtime::tests::processScopePreflightTests(),
            asharia::host_runtime::tests::processScopeStartupTests(),
            asharia::host_runtime::tests::processScopeCleanupTests(),
        };
        const bool succeeded =
            std::ranges::all_of(groups, [](std::span<const NamedProcessScopeTestV1> group) {
                return std::ranges::all_of(group, [](const NamedProcessScopeTestV1& test) {
                    if (test.function()) {
                        return true;
                    }
                    constexpr std::string_view prefix{"FAILED: "};
                    constexpr std::string_view newline{"\n"};
                    (void)std::fwrite(prefix.data(), 1, prefix.size(), stderr);
                    (void)std::fwrite(test.name.data(), 1, test.name.size(), stderr);
                    (void)std::fwrite(newline.data(), 1, newline.size(), stderr);
                    return false;
                });
            });
        return succeeded ? 0 : 1;
    } catch (...) {
        constexpr std::string_view message{"FAILED: unexpected ProcessScope test exception\n"};
        (void)std::fwrite(message.data(), 1, message.size(), stderr);
        return 1;
    }
}

#include <cstdlib>
#include <exception>
#include <iostream>

namespace asharia::rendergraph_header_tests {
    void touchCommandListHeader();
    void touchTypesHeader();
    void touchPassContextHeader();
    void touchExecutionHeader();
    void touchBuilderHeader();
    void touchCompileHeader();
    void touchDiagnosticsHeader();
    void touchAggregateHeader();
} // namespace asharia::rendergraph_header_tests

namespace {
    void runHeaderTests() {
        asharia::rendergraph_header_tests::touchCommandListHeader();
        asharia::rendergraph_header_tests::touchTypesHeader();
        asharia::rendergraph_header_tests::touchPassContextHeader();
        asharia::rendergraph_header_tests::touchExecutionHeader();
        asharia::rendergraph_header_tests::touchBuilderHeader();
        asharia::rendergraph_header_tests::touchCompileHeader();
        asharia::rendergraph_header_tests::touchDiagnosticsHeader();
        asharia::rendergraph_header_tests::touchAggregateHeader();

        std::cout << "RenderGraph header tests passed.\n";
    }
} // namespace

// The exhaustive catch boundary converts all failures to the header-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        runHeaderTests();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "RenderGraph header test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "RenderGraph header test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}

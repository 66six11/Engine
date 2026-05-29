#include <cstdlib>
#include <iostream>

namespace asharia::rendergraph_header_tests {
    void touchTypesHeader();
    void touchCompileHeader();
    void touchDiagnosticsHeader();
    void touchAggregateHeader();
} // namespace asharia::rendergraph_header_tests

int main() {
    asharia::rendergraph_header_tests::touchTypesHeader();
    asharia::rendergraph_header_tests::touchCompileHeader();
    asharia::rendergraph_header_tests::touchDiagnosticsHeader();
    asharia::rendergraph_header_tests::touchAggregateHeader();

    std::cout << "RenderGraph header tests passed.\n";
    return EXIT_SUCCESS;
}

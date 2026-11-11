#include "check.hpp"

std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> r;
    return r;
}

int g_failures = 0;

int main() {
    int failed_tests = 0;
    for (auto& t : test_registry()) {
        int before = g_failures;
        t.fn();
        bool ok = g_failures == before;
        if (!ok) ++failed_tests;
        std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", t.name);
    }
    std::printf("%zu tests, %d failed\n", test_registry().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}

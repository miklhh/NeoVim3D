// A minimal test harness.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& test_registry();
extern int g_failures;

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_registry().push_back({name, std::move(fn)});
    }
};

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                                  \
    static void TEST_CAT(test_fn_, name)();                                         \
    static TestRegistrar TEST_CAT(test_reg_, name)(#name, TEST_CAT(test_fn_, name)); \
    static void TEST_CAT(test_fn_, name)()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        auto _va = (a);                                                                \
        auto _vb = (b);                                                                \
        if (!(_va == _vb)) {                                                           \
            std::fprintf(stderr, "  %s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, \
                         #a, #b);                                                      \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

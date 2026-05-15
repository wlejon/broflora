#pragma once

// Shared test harness for broflora_test — same custom TEST(name) macro
// pattern as bromesh. Tests register at static-init time, dispatch from
// main() in test_main.cpp.

#include "broflora/broflora.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

extern int tests_run;
extern int tests_passed;

using TestFn = void(*)();
struct TestEntry { const char* name; TestFn fn; };
std::vector<TestEntry>& testRegistry();

struct TestRegistrar {
    TestRegistrar(const char* name, TestFn fn) {
        testRegistry().push_back({name, fn});
    }
};

#define TEST(name) \
    static void test_##name(); \
    static TestRegistrar reg_##name(#name, &test_##name); \
    static void test_##name()

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        tests_passed++; \
    } \
} while(0)

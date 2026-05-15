#include "test_framework.h"

int tests_run = 0;
int tests_passed = 0;

std::vector<TestEntry>& testRegistry() {
    static std::vector<TestEntry> r;
    return r;
}

int main() {
    for (auto& t : testRegistry()) {
        std::printf("[ run  ] %s\n", t.name);
        int before = tests_run;
        int beforePassed = tests_passed;
        t.fn();
        int ran = tests_run - before;
        int passed = tests_passed - beforePassed;
        std::printf("[ %s ] %s (%d/%d)\n",
            (ran == passed ? "pass" : "FAIL"), t.name, passed, ran);
    }

    std::printf("\n%d/%d assertions passed\n", tests_passed, tests_run);
    return tests_run == tests_passed ? 0 : 1;
}

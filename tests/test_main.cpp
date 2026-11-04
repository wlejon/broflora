#include "test_framework.h"

#include <cstring>

#ifdef _WIN32
#include <crtdbg.h>
#endif

int tests_run = 0;
int tests_passed = 0;

std::vector<TestEntry>& testRegistry() {
    static std::vector<TestEntry> r;
    return r;
}

// Supports `--filter=<substr>`, `--filter <substr>`, `-f <substr>`, a bare
// positional `<substr>` argument, or `--list` / `-l`. With no argument every
// test runs (the default CI invocation).
int main(int argc, char** argv) {
#ifdef _WIN32
    // Route CRT/STL debug assertions (e.g. a bounds-checked vector subscript in
    // a Debug build) to stderr instead of a modal dialog, so a crash fails fast
    // in the terminal or a Windows CI runner rather than hanging on a popup.
    for (int mode : {_CRT_ASSERT, _CRT_ERROR, _CRT_WARN}) {
        _CrtSetReportMode(mode, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(mode, _CRTDBG_FILE_STDERR);
    }
#endif

    const char* filter = nullptr;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--list") == 0 || std::strcmp(arg, "-l") == 0) {
            listOnly = true;
        } else if (std::strncmp(arg, "--filter=", 9) == 0) {
            filter = arg + 9;
        } else if ((std::strcmp(arg, "--filter") == 0 || std::strcmp(arg, "-f") == 0) && i + 1 < argc) {
            filter = argv[++i];
        } else if (arg[0] != '-') {
            filter = arg;
        }
    }

    if (listOnly) {
        for (const auto& t : testRegistry()) {
            if (filter && std::strstr(t.name, filter) == nullptr) continue;
            std::printf("%s\n", t.name);
        }
        return 0;
    }

    for (auto& t : testRegistry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) continue;
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

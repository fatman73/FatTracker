#include "test.h"

#include <cstring>

namespace ftest {
namespace {
struct Entry {
    const char* name;
    Fn fn;
};
Entry g_tests[1024];
int g_count = 0;
int g_failures = 0;
bool g_currentFailed = false;
const char* g_current = "";
}  // namespace

Reg::Reg(const char* name, Fn fn) {
    if (g_count < 1024) g_tests[g_count++] = {name, fn};
}

void fail(const char* file, int line, const char* expr) {
    std::printf("FAIL %s: %s:%d %s\n", g_current, file, line, expr);
    g_currentFailed = true;
}

void failEq(const char* file, int line, const char* expr, double a, double b) {
    std::printf("FAIL %s: %s:%d %s (got %.9g vs %.9g)\n", g_current, file, line, expr, a, b);
    g_currentFailed = true;
}
}  // namespace ftest

int runCli(int argc, char** argv);  // tests/cli.cpp (render/compare), weak default below

int main(int argc, char** argv) {
    using namespace ftest;
    if (argc >= 2 && (std::strcmp(argv[1], "render") == 0 || std::strcmp(argv[1], "compare") == 0))
        return runCli(argc, argv);
    int run = 0;
    for (int i = 0; i < g_count; ++i) {
        if (argc >= 2 && std::strcmp(argv[1], g_tests[i].name) != 0) continue;
        g_current = g_tests[i].name;
        g_currentFailed = false;
        g_tests[i].fn();
        ++run;
        if (g_currentFailed) ++g_failures;
    }
    if (run == 0) {
        std::printf("NO TESTS MATCHED\n");
        return 1;
    }
    if (g_failures == 0)
        std::printf("ALL TESTS PASSED (%d)\n", run);
    else
        std::printf("FAILED %d of %d\n", g_failures, run);
    return g_failures;
}

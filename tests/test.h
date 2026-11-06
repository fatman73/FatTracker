// Minimal test runner for fttest (no exceptions, no iostream).
#pragma once
#include <cmath>
#include <cstdio>

#include "compat.h"  // MSVC CRT shims outside Windows

namespace ftest {
using Fn = void (*)();
struct Reg {
    Reg(const char* name, Fn fn);
};
void fail(const char* file, int line, const char* expr);
void failEq(const char* file, int line, const char* expr, double a, double b);
}  // namespace ftest

#define TEST(name)                                         \
    static void name();                                    \
    static ftest::Reg name##_reg(#name, name);             \
    static void name()

#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) ftest::fail(__FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _a = (a);                                                        \
        auto _b = (b);                                                        \
        if (!(_a == _b))                                                      \
            ftest::failEq(__FILE__, __LINE__, #a " == " #b, (double)_a, (double)_b); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                 \
    do {                                                                      \
        double _a = (double)(a);                                              \
        double _b = (double)(b);                                              \
        if (!(std::fabs(_a - _b) <= (double)(eps)))                           \
            ftest::failEq(__FILE__, __LINE__, #a " ~= " #b, _a, _b);          \
    } while (0)

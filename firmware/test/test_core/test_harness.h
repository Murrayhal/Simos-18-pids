// Dependency-free assertion harness, so the same test binary runs under
// `pio test -e native` and under a plain g++ invocation in CI.
#pragma once

#include <stdio.h>

#include <string>
#include <vector>

namespace harness {

struct Failure {
  std::string test;
  std::string expr;
  std::string file;
  int line;
  std::string detail;
};

inline std::vector<Failure> &failures() {
  static std::vector<Failure> f;
  return f;
}

inline const char *&currentTest() {
  static const char *name = "";
  return name;
}

inline int &checkCount() {
  static int n = 0;
  return n;
}

inline void record(const char *expr, const char *file, int line,
                   const std::string &detail) {
  failures().push_back({currentTest(), expr, file, line, detail});
}

inline int report() {
  printf("\n%d checks, %d failures\n", checkCount(),
         static_cast<int>(failures().size()));
  for (const Failure &f : failures()) {
    printf("FAIL %s\n  %s:%d\n  %s\n", f.test.c_str(), f.file.c_str(), f.line,
           f.expr.c_str());
    if (!f.detail.empty()) printf("  %s\n", f.detail.c_str());
  }
  return failures().empty() ? 0 : 1;
}

}  // namespace harness

#define TEST(name)                                     \
  do {                                                 \
    harness::currentTest() = name;                     \
    printf("  %s\n", name);                            \
  } while (0)

#define CHECK(expr)                                             \
  do {                                                          \
    harness::checkCount()++;                                    \
    if (!(expr)) harness::record(#expr, __FILE__, __LINE__, ""); \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    harness::checkCount()++;                                               \
    const auto _a = (a);                                                   \
    const auto _b = (b);                                                   \
    if (!(_a == _b)) {                                                     \
      char _buf[160];                                                      \
      snprintf(_buf, sizeof(_buf), "got %lld, expected %lld",              \
               static_cast<long long>(_a), static_cast<long long>(_b));    \
      harness::record(#a " == " #b, __FILE__, __LINE__, _buf);             \
    }                                                                      \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                            \
  do {                                                                   \
    harness::checkCount()++;                                             \
    const double _a = (a);                                               \
    const double _b = (b);                                               \
    if (!((_a - _b) < (tol) && (_b - _a) < (tol))) {                     \
      char _buf[160];                                                    \
      snprintf(_buf, sizeof(_buf), "got %f, expected %f +/- %f", _a, _b,  \
               static_cast<double>(tol));                                \
      harness::record(#a " ~= " #b, __FILE__, __LINE__, _buf);           \
    }                                                                    \
  } while (0)

/* minunit.h - a minimal unit testing framework for C */
#ifndef MINUNIT_H
#define MINUNIT_H

#include <stdio.h>
#include <string.h>

#define mu_assert(message, test)                                               \
  do {                                                                         \
    if (!(test))                                                               \
      return message;                                                          \
  } while (0)
#define mu_assert_eq(expected, actual)                                         \
  do {                                                                         \
    if ((expected) != (actual)) {                                              \
      printf("  FAIL: expected %d but got %d at %s:%d\n", (int)(expected),     \
             (int)(actual), __FILE__, __LINE__);                               \
      return __FILE__;                                                         \
    }                                                                          \
  } while (0)
#define mu_assert_eq_uint32(expected, actual)                                  \
  do {                                                                         \
    if ((expected) != (actual)) {                                              \
      printf("  FAIL: expected %u but got %u at %s:%d\n",                      \
             (unsigned)(expected), (unsigned)(actual), __FILE__, __LINE__);    \
      return __FILE__;                                                         \
    }                                                                          \
  } while (0)
#define mu_assert_feq(expected, actual, eps)                                   \
  do {                                                                         \
    if (((actual) < (expected) - (eps)) || ((actual) > (expected) + (eps))) {  \
      printf("  FAIL: expected %.4f but got %.4f at %s:%d\n",                  \
             (double)(expected), (double)(actual), __FILE__, __LINE__);        \
      return __FILE__;                                                         \
    }                                                                          \
  } while (0)
#define mu_assert_mem_eq(expected, actual, len)                                \
  do {                                                                         \
    if (memcmp((expected), (actual), (len)) != 0) {                            \
      printf("  FAIL: memory mismatch at %s:%d\n", __FILE__, __LINE__);        \
      return __FILE__;                                                         \
    }                                                                          \
  } while (0)

#define mu_run_test(test)                                                      \
  do {                                                                         \
    const char *message = test();                                              \
    tests_run++;                                                               \
    if (message) {                                                             \
      printf("  FAILED: %s\n", message);                                       \
      tests_failed++;                                                          \
    } else {                                                                   \
      printf("  PASSED: %s\n", #test);                                         \
    }                                                                          \
  } while (0)

extern int tests_run;
extern int tests_failed;

#endif /* MINUNIT_H */

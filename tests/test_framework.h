/* tests/test_framework.h — minimal test framework, no external dependencies */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int _tests_run    = 0;
static int _tests_failed = 0;
static int _tests_passed = 0;

#define ASSERT(cond) do { \
    _tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    _tests_run++; \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL %s:%d  %s == %s  (got %lld, expected %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    _tests_run++; \
    if ((a) == (b)) { \
        fprintf(stderr, "  FAIL %s:%d  %s != %s  (both == %lld)\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a)); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

#define ASSERT_NULL(p) do { \
    _tests_run++; \
    if ((p) != NULL) { \
        fprintf(stderr, "  FAIL %s:%d  expected NULL: %s\n", \
                __FILE__, __LINE__, #p); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    _tests_run++; \
    if ((p) == NULL) { \
        fprintf(stderr, "  FAIL %s:%d  unexpected NULL: %s\n", \
                __FILE__, __LINE__, #p); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, n) do { \
    _tests_run++; \
    if (memcmp((a), (b), (n)) != 0) { \
        fprintf(stderr, "  FAIL %s:%d  memcmp(%s, %s, %zu) != 0\n", \
                __FILE__, __LINE__, #a, #b, (size_t)(n)); \
        _tests_failed++; \
    } else { \
        _tests_passed++; \
    } \
} while(0)

/* Run a named test function and print its result */
#define RUN_TEST(fn) do { \
    int _before_fail = _tests_failed; \
    printf("[ RUN ] %s\n", #fn); \
    fn(); \
    if (_tests_failed > _before_fail) \
        printf("[ FAIL] %s\n", #fn); \
    else \
        printf("[ OK  ] %s\n", #fn); \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n%d tests, %d passed, %d failed\n", \
           _tests_run, _tests_passed, _tests_failed); \
    return _tests_failed ? EXIT_FAILURE : EXIT_SUCCESS; \
} while(0)

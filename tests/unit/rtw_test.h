#ifndef RTW_TEST_H
#define RTW_TEST_H

#include <stdio.h>
#include <stdlib.h>

static int rtw_tests_failed = 0;
static int rtw_tests_passed = 0;

#define RTW_CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        rtw_tests_failed++; \
    } else { \
        rtw_tests_passed++; \
    } \
} while (0)

#define RTW_TEST_MAIN(fn) int main(void) { \
    fn(); \
    fprintf(stderr, "passed=%d failed=%d\n", rtw_tests_passed, rtw_tests_failed); \
    return rtw_tests_failed ? 1 : 0; \
}

#endif

/* Helpers for the C API tests: an always-on CHECK and a latch, which C99 has no threads for. */
#ifndef C_TEST_SUPPORT_H
#define C_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Unlike assert, also checks in Release builds. */
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
            abort();                                                                   \
        }                                                                              \
    } while (0)

/* Counts signals; each wait consumes one, blocking until it arrives. */
typedef struct test_latch test_latch;

test_latch *test_latch_new(void);
void test_latch_signal(test_latch *latch);
void test_latch_wait(test_latch *latch);
/* Signals the latch from a new thread, so the caller can block before it arrives. */
void test_latch_signal_from_thread(test_latch *latch);
void test_latch_free(test_latch *latch);

#ifdef __cplusplus
}
#endif

#endif

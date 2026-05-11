#include "kunit.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void kunit_init(struct kunit *test, const char *suite_name, const char *case_name) {
    if (!test) {
        return;
    }

    memset(test, 0, sizeof(*test));
    test->suite_name = suite_name;
    test->case_name = case_name;
}

void kunit_failf(struct kunit *test, const char *file, int line, const char *format, ...) {
    va_list args;

    if (!test || test->failed) {
        return;
    }

    test->failed = 1;
    test->failure_file = file;
    test->failure_line = line;

    va_start(args, format);
    vsnprintf(test->failure_message, sizeof(test->failure_message), format, args);
    va_end(args);
}

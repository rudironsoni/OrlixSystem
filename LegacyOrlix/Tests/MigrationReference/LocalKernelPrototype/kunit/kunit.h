#ifndef ORLIX_KERNEL_TESTS_KUNIT_H
#define ORLIX_KERNEL_TESTS_KUNIT_H

struct kunit {
    int failed;
    const char *suite_name;
    const char *case_name;
    const char *failure_file;
    int failure_line;
    char failure_message[512];
};

struct kunit_case {
    const char *name;
    void (*run_case)(struct kunit *test);
};

struct kunit_suite {
    const char *name;
    const struct kunit_case *cases;
    unsigned long case_count;
    void (*init)(struct kunit *test);
    void (*exit)(struct kunit *test);
};

#define KUNIT_CASE(function_name) \
    { .name = #function_name, .run_case = (function_name) }

void kunit_init(struct kunit *test, const char *suite_name, const char *case_name);
void kunit_failf(struct kunit *test, const char *file, int line, const char *format, ...);

static inline int kunit_streq(const char *lhs, const char *rhs) {
    unsigned long index = 0;

    if (lhs == rhs) {
        return 1;
    }
    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index]) {
            return 0;
        }
        index++;
    }
    return lhs[index] == rhs[index];
}

#define KUNIT_FAIL(test, format, ...)                                             \
    do {                                                                          \
        kunit_failf((test), __FILE__, __LINE__, (format), ##__VA_ARGS__);         \
        return;                                                                   \
    } while (0)

#define KUNIT_ASSERT_TRUE(test, condition)                                        \
    do {                                                                          \
        if (!(condition)) {                                                       \
            KUNIT_FAIL((test), "assertion failed: %s", #condition);               \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_TRUE KUNIT_ASSERT_TRUE

#define KUNIT_ASSERT_FALSE(test, condition)                                       \
    do {                                                                          \
        if ((condition)) {                                                        \
            KUNIT_FAIL((test), "assertion failed: !(%s)", #condition);           \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_FALSE KUNIT_ASSERT_FALSE

#define KUNIT_ASSERT_EQ(test, actual_value, expected_value)                       \
    do {                                                                          \
        long long kunit_actual = (long long)(actual_value);                       \
        long long kunit_expected = (long long)(expected_value);                   \
        if (kunit_actual != kunit_expected) {                                     \
            KUNIT_FAIL((test),                                                    \
                       "expected %s == %s, got %lld and %lld",                    \
                       #actual_value,                                             \
                       #expected_value,                                           \
                       kunit_actual,                                              \
                       kunit_expected);                                           \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_EQ KUNIT_ASSERT_EQ

#define KUNIT_ASSERT_NE(test, actual_value, unexpected_value)                     \
    do {                                                                          \
        long long kunit_actual = (long long)(actual_value);                       \
        long long kunit_unexpected = (long long)(unexpected_value);               \
        if (kunit_actual == kunit_unexpected) {                                   \
            KUNIT_FAIL((test),                                                    \
                       "expected %s != %s, both were %lld",                       \
                       #actual_value,                                             \
                       #unexpected_value,                                         \
                       kunit_actual);                                             \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_NE KUNIT_ASSERT_NE

#define KUNIT_ASSERT_NOT_NULL(test, ptr)                                          \
    do {                                                                          \
        const void *kunit_ptr = (const void *)(unsigned long)(ptr);               \
        if (kunit_ptr == ((void *)0)) {                                           \
            KUNIT_FAIL((test), "expected %s to be non-NULL", #ptr);              \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_NOT_NULL KUNIT_ASSERT_NOT_NULL

#define KUNIT_ASSERT_STREQ(test, actual_value, expected_value)                    \
    do {                                                                          \
        const char *kunit_actual = (actual_value);                                \
        const char *kunit_expected = (expected_value);                            \
        if (!kunit_streq(kunit_actual, kunit_expected)) {                         \
            KUNIT_FAIL((test),                                                    \
                       "expected %s == %s, got \"%s\" and \"%s\"",               \
                       #actual_value,                                             \
                       #expected_value,                                           \
                       kunit_actual ? kunit_actual : "(null)",                    \
                       kunit_expected ? kunit_expected : "(null)");               \
        }                                                                         \
    } while (0)

#define KUNIT_EXPECT_STREQ KUNIT_ASSERT_STREQ

#endif

#ifndef SWIMD_CUNIT_H
#define SWIMD_CUNIT_H

#define SWIMD_CUNIT_TEST_OUT "tests/out/"

typedef struct {
    bool success;
    const char *message;
} SwimdCunitTestResult;

typedef void (*unit_test)(SwimdCunitTestResult *result);
typedef struct {
    const char *name;
    unit_test fn;
} SwimdCunitTest;

bool swimd_cunit_run(SwimdCunitTest *unit_tests, int unit_tests_length);
void swimd_cunit_fail(SwimdCunitTestResult *res, const char *reason);

#endif

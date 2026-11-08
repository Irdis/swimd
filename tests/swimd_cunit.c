#include <stdio.h>

#include "swimd_cunit.h"

bool swimd_cunit_run(SwimdCunitTest *unit_tests, int unit_tests_length) {
    int fail_count = 0;
    for (int i = 0; i < unit_tests_length; i++) {
        SwimdCunitTest *test = &unit_tests[i];
        SwimdCunitTestResult res = {0};
        res.success = true;
        printf("=> %s\n", test->name);
        test->fn(&res);
        if (res.success) {
            printf("[OK] => %s\n", test->name);
        } else {
            printf("[ERR] => %s\n", test->name);
            printf(res.message);
            printf("\n");
            fail_count++;
        }
    }
    printf("\nTest results: Success %d, Fail %d, All %d\n", unit_tests_length - fail_count, fail_count, unit_tests_length);
    return fail_count > 0;
}

void swimd_cunit_fail(SwimdCunitTestResult *res, const char *reason) {
    res->success = false;
    res->message = reason;
}

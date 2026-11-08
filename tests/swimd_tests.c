#include <stdio.h>

#include "swimd_cunit.h"
#include "swimd_scenarios_watch.c"

#define NOB_IMPLEMENTATION
#undef ERROR
#include "../nob.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static SwimdCunitTest swimd_tests[] = {
    { .name = "swimd_scenarios_watch", .fn = &swimd_scenarios_watch}
};

int main() {
    if (!swimd_cunit_run(swimd_tests, ARRAY_SIZE(swimd_tests))) {
        return 1;
    }
    return 0;
}

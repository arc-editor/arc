#define _POSIX_C_SOURCE 199309L

#include <time.h>
#include <stdlib.h>
#include "perf.h"
#include "log.h"

struct timespec ts;
const char *curr_name = NULL;

void perf_start(const char *name) {
    curr_name = name;
    clock_gettime(CLOCK_MONOTONIC, &ts);
}

void perf_end() {
    if (curr_name == NULL) {
        log_error("perf.perf_end: curr_name is NULL");
        exit(1);
    }
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ns = (end.tv_sec - ts.tv_sec) * 1000000000L + (end.tv_nsec - ts.tv_nsec);
    log_info("perf.perf_end: %s: %.3f μs", curr_name, elapsed_ns / 1000.0);
}


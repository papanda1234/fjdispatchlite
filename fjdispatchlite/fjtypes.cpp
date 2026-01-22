#include "fjtypes.h"

/**
 * @brief epoch time
 */
int64_t _get_time() {
    int64_t timeMs;
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
    timeMs = ((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000);
    return timeMs;
}

/**
 * @brief convert to timespec
 */
void _get_future_timespec(struct timespec *ts_out, int64_t timeout_msec) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    ts_out->tv_sec = now.tv_sec + timeout_msec / 1000;
    ts_out->tv_nsec = now.tv_nsec + (timeout_msec % 1000) * 1000000ULL;
    if (ts_out->tv_nsec >= 1000000000ULL) {
        ts_out->tv_sec += 1;
        ts_out->tv_nsec -= 1000000000ULL;
    }
}

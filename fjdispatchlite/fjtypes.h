/**
 * Copyright 2025 FJD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __FJTYPES_H__
#define __FJTYPES_H__

#ifndef DOXYGEN_SKIP_THIS
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#endif

#ifndef __FJT_TIME__
#define __FJT_TIME__
typedef int64_t fjt_time_t; //!< 時刻型
#endif

#ifndef __FJT_HANDLE_T__
#define __FJT_HANDLE_T__
typedef uint64_t fjt_handle_t; //!< ハンドル型
#endif

#ifndef __FJT_MSG_T__
#define __FJT_MSG_T__
typedef uint32_t fjt_msg_t; //!< メッセージID型
#endif

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

#ifndef COLOR_RED
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RESET   "\033[0m"
#endif

#endif

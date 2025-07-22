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

/**
 * @file fjmediaqueue.h
 * @author FJD
 * @date 2025.6.24
 */
#ifndef __FJMEDIAQUEUE_H__
#define __FJMEDIAQUEUE_H__

#include <stdint.h>
#include <string.h>
#include <string>
#include <pthread.h>
#include <thread>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

class FJMediaQueue {
public:
    FJMediaQueue(const std::string &shm_name, uint32_t elem_size, uint32_t elem_count);
    ~FJMediaQueue();

    FJMediaQueue(const FJMediaQueue &) = delete;
    FJMediaQueue &operator=(const FJMediaQueue &) = delete;

    int write(char *buf, uint32_t size, uint64_t timestamp, uint32_t waitmsec);
    int read(char *buf, uint32_t &size, uint64_t &timestamp, uint32_t waitmsec);
    void control(bool start);
    int timedwait(uint32_t waitmsec);

private:
    struct RINGATOM {
        char *buf;
        uint32_t size;
        uint64_t timestamp;
    };

    struct SharedMemoryLayout {
        int rptr;
        int wptr;
        bool control_flag;
        sem_t rsem;
        sem_t wsem;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        // RINGATOMとデータ本体はこの構造体の直後に確保される
    };

    size_t calculate_shm_size(uint32_t elem_size, uint32_t elem_count);

    int shm_fd_ = -1;
    std::string shm_name_;
    uint32_t elem_size_;
    uint32_t elem_count_;
    SharedMemoryLayout *shm_ = nullptr;
    RINGATOM *atoms_ = nullptr;
    char *data_ = nullptr;
};
#endif //__FJMEDIAQUEUE_H__

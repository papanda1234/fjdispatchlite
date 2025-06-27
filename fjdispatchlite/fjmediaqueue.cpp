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
 * @file fjmediaqueue.cpp
 * @author FJD
 * @date 2025.6.24
 */

#include "fjmediaqueue.h"

size_t FJMediaQueue::calculate_shm_size(uint32_t elem_size, uint32_t elem_count) {
    return sizeof(SharedMemoryLayout) + sizeof(RINGATOM) * elem_count + elem_size * elem_count;
}

FJMediaQueue::FJMediaQueue(const std::string &shm_name, uint32_t elem_size, uint32_t elem_count)
    : shm_name_(shm_name), elem_size_(elem_size), elem_count_(elem_count) {
    bool creator = false;

    shm_fd_ = shm_open(shm_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
    if (shm_fd_ >= 0) {
        creator = true;
        size_t total_size = calculate_shm_size(elem_size, elem_count);
        if (ftruncate(shm_fd_, total_size) < 0)
            throw std::runtime_error("Failed to truncate shared memory");
    } else {
        shm_fd_ = shm_open(shm_name.c_str(), O_RDWR, 0666);
        if (shm_fd_ < 0)
            throw std::runtime_error("Failed to open shared memory");
    }

    size_t total_size = calculate_shm_size(elem_size_, elem_count_);
    void *base = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (base == MAP_FAILED)
        throw std::runtime_error("Failed to mmap");

    shm_ = static_cast<SharedMemoryLayout *>(base);
    atoms_ = reinterpret_cast<RINGATOM *>((char *)base + sizeof(SharedMemoryLayout));
    data_ = reinterpret_cast<char *>((char *)atoms_ + sizeof(RINGATOM) * elem_count_);

    if (creator) {
        shm_->rptr = 0;
        shm_->wptr = 0;
        shm_->control_flag = true;
        sem_init(&shm_->rsem, 1, 0);
        sem_init(&shm_->wsem, 1, elem_count_);
        pthread_mutexattr_t mattr;
        pthread_condattr_t cattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm_->mutex, &mattr);
        pthread_mutexattr_destroy(&mattr);
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&shm_->cond, &cattr);
        pthread_condattr_destroy(&cattr);

        for (uint32_t i = 0; i < elem_count_; ++i) {
            atoms_[i].buf = data_ + i * elem_size_;
            atoms_[i].size = 0;
            atoms_[i].timestamp = 0;
        }
    } else {
        for (uint32_t i = 0; i < elem_count_; ++i) {
            atoms_[i].buf = data_ + i * elem_size_;
        }
    }
}

FJMediaQueue::~FJMediaQueue() {
    if (shm_) {
        size_t total_size = calculate_shm_size(elem_size_, elem_count_);
        munmap(shm_, total_size);
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
    }
}

void FJMediaQueue::control(bool start) {
    shm_->control_flag = start;
}

int FJMediaQueue::write(char *buf, uint32_t size, uint64_t timestamp, uint32_t waitmsec) {
    if (!shm_->control_flag) return -3;
    if (!buf || size > elem_size_) return -2;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitmsec);
    uint32_t wait_step = 1;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sem_trywait(&shm_->wsem) == 0) {
            int index = shm_->wptr;
            memcpy(atoms_[index].buf, buf, size);
            atoms_[index].size = size;
            atoms_[index].timestamp = timestamp;
            shm_->wptr = (shm_->wptr + 1) % elem_count_;
            sem_post(&shm_->rsem);
            pthread_mutex_lock(&shm_->mutex);
            pthread_cond_signal(&shm_->cond);
            pthread_mutex_unlock(&shm_->mutex);
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_step++));
    }
    return -1;
}

int FJMediaQueue::read(char *buf, uint32_t &size, uint64_t &timestamp, uint32_t waitmsec) {
    if (!buf) return -2;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitmsec);
    uint32_t wait_step = 1;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sem_trywait(&shm_->rsem) == 0) {
            int index = shm_->rptr;
            size = atoms_[index].size;
            timestamp = atoms_[index].timestamp;
            memcpy(buf, atoms_[index].buf, size);
            shm_->rptr = (shm_->rptr + 1) % elem_count_;
            sem_post(&shm_->wsem);
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_step++));
    }
    return -1;
}

int FJMediaQueue::timedwait(uint32_t waitmsec) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += waitmsec / 1000;
    ts.tv_nsec += (waitmsec % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&shm_->mutex);
    int rc = pthread_cond_timedwait(&shm_->cond, &shm_->mutex, &ts);
    pthread_mutex_unlock(&shm_->mutex);
    return rc;
}


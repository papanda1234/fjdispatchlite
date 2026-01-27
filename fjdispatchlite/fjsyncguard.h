#ifndef __FJSYNCGUARD_H__
#define __FJSYNCGUARD_H__

#ifndef DOXYGEN_SKIP_THIS
#include <pthread.h>
#endif

#include "fjsharedmem.h"

class FJMutex {
public:
    explicit FJMutex(pthread_mutex_t *mutex) : mutex_(mutex) {
	if (mutex_) pthread_mutex_lock(mutex_);
    }
    ~FJMutex() {
	if (mutex_) pthread_mutex_unlock(mutex_);
    }

    // コピー禁止
    FJMutex(const FJMutex&) = delete;
    FJMutex& operator=(const FJMutex&) = delete;
private:
    pthread_mutex_t *mutex_;
};

class FJSyncGuard {
public:
    explicit FJSyncGuard(FJSharedMem* obj) : obj_(obj) {
		if (!obj_) return;
		lock_ = obj_->_lock();
    }
    ~FJSyncGuard() {
		if (lock_) {
			obj_->_unlock();
		}
    }

    // コピー禁止
    FJSyncGuard(const FJSyncGuard&) = delete;
    FJSyncGuard& operator=(const FJSyncGuard&) = delete;

private:
	bool lock_;
	FJSharedMem *obj_;
};

#endif

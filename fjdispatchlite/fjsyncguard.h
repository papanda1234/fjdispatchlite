#ifndef __FJSYNCGUARD_H__
#define __FJSYNCGUARD_H__

#include "fjsharedmem.h"

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

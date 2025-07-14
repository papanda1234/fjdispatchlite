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
 * @file fjsharedmem.h
 * @author FJD
 * @date 2025.7.7
 */

#ifndef __FJSHAREDMEM_H__
#define __FJSHAREDMEM_H__

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fjtypes.h"
#include "fjfixvector.h"

#define C_FJNT_LISTEN_MAX 256 //!< リスナーテーブル最大数
#define C_FJNT_QUEUE_MAX 512 //!< メッセージキュー最大数

#define FJSHAREDMEM_DBG (0) //!< for debug.

/**
 * @brief メッセージ通知付き共有メモリ
 */
class FJSharedMem {
public:
    /**
     * @brief メール形式
     */
    struct mailAtom {
        fjt_msg_t msg_;
        FJSharedMem* obj_;
        pid_t pid_;
    };

    /**
     * @brief 管理領域
     */
    struct SharedRegion {
	uint32_t initialized_;
        pthread_mutex_t mutex_;
        pthread_cond_t cond_;
	uint32_t lptr_;
	uint32_t qptr_;
        mailAtom listen_[C_FJNT_LISTEN_MAX];
        mailAtom queue_[C_FJNT_QUEUE_MAX];
    };

    /**
     * @brief コンストラクタ
     * @param[in] shm_name 共有メモリの名前。必ず'/'で始めること。
     * @param[in] extra_size 拡張領域のサイズ(初期化後get()で取得可能)
     */
    FJSharedMem(const std::string& shm_name, size_t extra_size, const char *srcfunc = nullptr, std::vector<fjt_msg_t> list = {})
        : pid_(getpid()), shm_name_(shm_name), full_ptr_(nullptr), user_ptr_(nullptr), worker_done_(false), needworker_(false) 
    {
	if (srcfunc) {
	    srcfunc_ = srcfunc;
	}
        total_size_ = sizeof(SharedRegion) + extra_size;
        bool is_create = false;
        int fd = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (fd < 0) {
	    // まだ存在しない
            fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
            if (fd < 0) { perror("shm_open"); exit(1); }
            if (ftruncate(fd, total_size_) < 0) { perror("ftruncate"); exit(1); }
	    is_create = true;
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): created sharedmem[" << shm_name_ << "]" << COLOR_RESET << std::endl;
#endif
        }

        full_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (full_ptr_ == MAP_FAILED) { perror("mmap"); exit(1); }
	close(fd);

        shared_region_ = reinterpret_cast<SharedRegion*>(full_ptr_);
        user_ptr_ = reinterpret_cast<void*>((char*)full_ptr_ + sizeof(SharedRegion));
        if (is_create) {
            initSharedRegion();
	} else {
	    uint32_t i = 0;
	    static uint32_t timeoutMs = 100;
	    for (; i < timeoutMs && shared_region_->initialized_ == 0; i++) {
		usleep(1000);

	    }
#if FJSHAREDMEM_DBG == 1
	    if (i == timeoutMs) {
		std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << "): WARNING: Timeout occurred while waiting for master initialization. " << COLOR_RESET << std::endl;
	    }
#endif
	}

	pthread_mutex_lock(&shared_region_->mutex_);

	int regi = _addListen(this, list);
	if (regi > 0) {
	    needworker_ = true;
	    ++p_refcount_;
	    if (p_refcount_ == 1) {
		worker_done_ = false;
		pthread_create(&worker_, nullptr, &FJSharedMem::workerThreadWrapper, this);
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): created worker thread" << COLOR_RESET << std::endl;
#endif
	    }
	}
	++(shared_region_->initialized_);

	pthread_mutex_unlock(&shared_region_->mutex_);
    }

    /**
     * @brief デストラクタ
     */
    virtual ~FJSharedMem() {
        if (full_ptr_) {
	    bool is_stopworker = false;
	    bool is_unlink = false;
	    pthread_mutex_lock(&shared_region_->mutex_);
	    if (needworker_) {
		--p_refcount_;
		if (p_refcount_ == 0) {
		    is_stopworker = true;
		    pthread_cond_broadcast(&shared_region_->cond_);
		}
	    }
	    pthread_mutex_unlock(&shared_region_->mutex_);

	    if (is_stopworker) {
		static uint32_t timeoutMs = 100;
		uint32_t i = 0;
		for (; i < timeoutMs && worker_done_ == false; ++i) {
		    usleep(1000);
		}
		if (i == timeoutMs) {
		    std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << "): thread join timeout." << COLOR_RESET << std::endl;
		}
		pthread_join(worker_, NULL);
	    }

	    pthread_mutex_lock(&shared_region_->mutex_);
	    if (needworker_) {
		FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
		for (int32_t i = 0; i < shared_region_->qptr_; ++i) {
		    mailAtom *m = queue.at(i);
		    if (m->obj_ == this) {
			m->obj_ = nullptr;
			m->msg_ = 0;
			m->pid_ = 0;
		    }
		}
		queue.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
		while (queue.length() > 0 && queue.at(0)->msg_ == 0) {
		    queue.splice(0, 1);
		}
		shared_region_->qptr_ = queue.length();

		FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
		for (int32_t i = 0; i < shared_region_->lptr_; ++i) {
		    mailAtom *m = listen.at(i);
		    if (m->obj_ == this) {
			m->obj_ = nullptr;
			m->msg_ = 0;
			m->pid_ = 0;
		    }
		}
		listen.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
		while (listen.length() > 0 && listen.at(0)->msg_ == 0) {
		    listen.splice(0, 1);
		}
		shared_region_->lptr_ = listen.length();
	    }

	    --(shared_region_->initialized_);
	    if (shared_region_->initialized_ == 0) {
		is_unlink = true;
	    }
	    pthread_mutex_unlock(&shared_region_->mutex_);

	    munmap(full_ptr_, total_size_);
	    if (is_unlink) {
		shm_unlink(shm_name_.c_str());
	    }
	}
    }

    /**
     * @brief 拡張領域のポインタ
     * @note void型であるため継承先のキャストが必要
     */
    void *_get() {
	void *ptr = nullptr;
	if (p_refcount_ > 0) {
	    pthread_mutex_lock(&shared_region_->mutex_);
	    ptr = user_ptr_;
	    pthread_mutex_unlock(&shared_region_->mutex_);
	}
	return ptr;
    }

    /**
     * @brief 受け取りたい通知を設定(スカラー)
     * @param[in] obj リスナーのオブジェクト(通常は自分自身)
     * @param[in] msg メッセージID
     */
    bool addListen(FJSharedMem* obj, fjt_msg_t msg) {
        if (!obj || msg == 0) return false;

        pthread_mutex_lock(&shared_region_->mutex_);
		if (needworker_ == false) {
			pthread_mutex_unlock(&shared_region_->mutex_);
			std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): This msg["<< msg << "] rejected!" << COLOR_RESET << std::endl;
			return false;
		}
		int regi = _addListen(obj, { msg });
        pthread_mutex_unlock(&shared_region_->mutex_);
        return (regi > 0);
    }

    /**
     * @brief 受け取りたい通知を設定(リスト)
     * @param[in] obj リスナーのオブジェクト(通常は自分自身)
     * @param[in] msg メッセージID
     */
    bool addListen(FJSharedMem* obj, std::vector<fjt_msg_t> list) {
        if (!obj) return false;

        pthread_mutex_lock(&shared_region_->mutex_);
		if (needworker_ == false) {
			pthread_mutex_unlock(&shared_region_->mutex_);
			std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): This list rejected!" << COLOR_RESET << std::endl;
			return false;
		}
		int regi = _addListen(obj, list);
        pthread_mutex_unlock(&shared_region_->mutex_);
        return (regi > 0);
    }

    /**
     * @brief 通知を送信(without lock)
     * @param[in] from センダーのオブジェクト(通常は自分自身)
     * @param[in] msg メッセージID
     */
    bool _notify(FJSharedMem* from, fjt_msg_t msg) {
        if (!from || msg == 0) return false;
	uint32_t msgcount = 0;
	FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
	FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
	// lesten_を0からlptr_までループするfor文
	for (uint32_t i = 0; i < shared_region_->lptr_; ++i) {
	    mailAtom *to = listen.at(i);
	    // 自分自身にはメッセージ送らない
	    if (to->msg_ == msg && to->obj_ != from) {
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << from << "): send msg[" << msg << "] to:" << to->obj_ << COLOR_RESET << std::endl;
#endif
		if (queue.push_back(*to) == false) {
		    std::cerr << COLOR_RED << "ERROR: " << srcfunc_ << "(pid:" << pid_ << " obj:" << from << "): msg[" << msg << "] to:" << to->obj_ << " queue is full." << COLOR_RESET << std::endl;
		}
		shared_region_->qptr_ = queue.length();
		++msgcount;
	    }
	}
	// ブロードキャスト
	if (msgcount > 0) {
	    pthread_cond_broadcast(&shared_region_->cond_);
	}
        return true;
    }

    /**
     * @brief 通知を送信(lock)
     * @param[in] from センダーのオブジェクト(通常は自分自身)
     * @param[in] msg メッセージID
     */
    bool notify(FJSharedMem* from, fjt_msg_t msg) {
	bool ret = false;
	pthread_mutex_lock(&shared_region_->mutex_);
	ret = _notify(from, msg);
	pthread_mutex_unlock(&shared_region_->mutex_);
	return ret;
    }

    /**
     * @brief 通知を受け取る関数
     */
    virtual void update(FJSharedMem* obj, fjt_msg_t msg) {};

protected:
    void* user_ptr_; //!< 拡張領域のポインタ
    pid_t pid_; //!< プロセスID

    friend class FJSyncGuard;

    /**
     * @brief FJSyncGuard's lock
     */  
    bool _lock() {
	if (shared_region_ && shared_region_->initialized_ > 0) {
	    if (pthread_mutex_trylock(&shared_region_->mutex_) == 0) {
		return true;
	    }
	}
	return false;
    }

    /**
     * @brief FJSyncGuard's unlock
     */  
    bool _unlock() {
	if (shared_region_ && shared_region_->initialized_ > 0) {
	    pthread_mutex_unlock(&shared_region_->mutex_);
	    return true;
	}
    }

private:
    /**
     * @brief 管理領域初期化
     */
    void initSharedRegion() {
        static bool initialized = false;
        if (initialized) return;
        initialized = true;

        pthread_mutexattr_t mattr;
        pthread_condattr_t cattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

        pthread_mutex_init(&shared_region_->mutex_, &mattr);
        pthread_cond_init(&shared_region_->cond_, &cattr);
        memset(shared_region_->listen_, 0, sizeof(shared_region_->listen_));
        shared_region_->lptr_ = 0;
        memset(shared_region_->queue_, 0, sizeof(shared_region_->queue_));
        shared_region_->qptr_ = 0;
	shared_region_->initialized_ = 0;
    }
	
    /**
     * @brief 受け取りたい通知を設定(internal)
     * @param[in] obj リスナーのオブジェクト(通常は自分自身)
     * @param[in] list メッセージIDリスト
     * @return 登録数
     */
    int _addListen(FJSharedMem *obj, std::vector<fjt_msg_t> list) {
	int regi = 0;
    	FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
	std::vector<fjt_msg_t>::iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
	    fjt_msg_t msg = *it;
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): addlisten msg[" << msg << "]" << COLOR_RESET << std::endl;
#endif
	    bool duplicated = false;
	    for (uint32_t i = 0; i < shared_region_->lptr_; ++i) {
		mailAtom *to = listen.at(i);
		if (to->obj_ == obj && to->msg_ == msg && to->pid_ == pid_) {
		    std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): This msg["<< msg << "] already registered!" << COLOR_RESET << std::endl;
		    duplicated = true;
		    break;
		}
	    }
	    if (duplicated == false) {
		mailAtom add = { msg, obj, pid_ };
		listen.push_back(add);
		regi++;
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): msg["<< msg << "] registered." << COLOR_RESET << std::endl;
#endif
	    }
	}
	listen.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
	shared_region_->lptr_ = listen.length();
	return regi;
    }

    /**
     * @brief ワーカースレッドラッパー
     * @param[in] arg インスタンスのポインタ
     */
    static void *workerThreadWrapper(void *arg) {
	FJSharedMem *self = static_cast<FJSharedMem*>(arg);
	self->workerThread();
	return nullptr;
    }

    /**
     * @brief ワーカースレッド
     * @note 1プロセスにつき1つ
     */
    void workerThread() {

        while (p_refcount_ > 0) {
            pthread_mutex_lock(&shared_region_->mutex_);
            pthread_cond_wait(&shared_region_->cond_, &shared_region_->mutex_);
	    if (p_refcount_ == 0) break;

	    FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
            std::vector<mailAtom> locals;
	    for (int32_t i = 0; i < shared_region_->qptr_; ++i) {
		// queue_を0からqptr_までループするfor文
		mailAtom *to = queue.at(i);
		if (to->msg_ != 0 && to->obj_ != nullptr && to->pid_ == pid_) {
		    // 自分のプロセス内のメッセージに限る
		    locals.push_back(*to);
		    to->obj_ = nullptr;
		    to->msg_ = 0;
		    to->pid_ = 0;
		}
	    }
	    queue.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
	    while (queue.length() > 0 && queue.at(0)->msg_ == 0) {
		queue.splice(0, 1);
	    }
	    shared_region_->qptr_ = queue.length();
            pthread_mutex_unlock(&shared_region_->mutex_);

            for (auto& mail : locals) {
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_YELLOW << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << this << "): received msg[" << mail.msg_ << "] from " << mail.obj_ << COLOR_RESET << std::endl;
#endif
                if (mail.obj_) mail.obj_->update(mail.obj_, mail.msg_);
		std::cerr << "OK[" << mail.msg_ << "]" << std::endl;
            }

        }
#if FJSHAREDMEM_DBG == 1
	std::cerr << COLOR_RED << "pid:" << pid_ << "'s workerThread ended." << COLOR_RESET << std::endl;
#endif	
	worker_done_ = true;
    }

    std::string shm_name_; //!< 共有メモリの名前
    std::string srcfunc_; //!< 共有メモリを作成した関数名
    void* full_ptr_; //!< 共有メモリの先頭
    SharedRegion* shared_region_; //!< 管理構造体
    size_t total_size_; //!< 総サイズ

    bool needworker_; ////!< ワーカースレッド必要フラグ
    pthread_t worker_; //!< ワーカースレッド
    bool worker_done_; //!< ワーカースレッド終了フラグ

    static int p_refcount_; //!< プロセス内でのFJSharedMemインスタンスの数
};

#endif //__FJSHAREDMEM_H__

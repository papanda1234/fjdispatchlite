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
 * @date 2025.7.16
 */

#ifndef __FJSHAREDMEM_H__
#define __FJSHAREDMEM_H__

#include <iostream>
#include <string>
#include <vector>
#include <map>
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
#include "fjfixmap.h"

#define C_FJNT_LISTEN_MAX 256 //!< リスナーテーブル最大数
#define C_FJNT_QUEUE_MAX 512 //!< メッセージキュー最大数
#define C_FJNT_PROCESS_MAX 50 //!< 共有メモリを利用するプロセスの最大数

#define C_FJNT_SHAREDREGION_NAME "/fjsharedmem" //!< 管理領域の名前

#define FJSHAREDMEM_DBG (0) //!< for debug.

/**
 * @brief メッセージ通知付き共有メモリ
 */
class FJSharedMem {
public:
    /**
     * @brief メール形式
     * @note msg_ == 0が空の条件であることに注意
     */
    struct mailAtom {
        fjt_msg_t msg_;
        FJSharedMem* obj_;
        pid_t pid_;
    };

    /**
     * @brief プロセスごとの管理領域
     */     
    struct proAtom {
        pthread_mutex_t mutex_;
        pthread_cond_t cond_;
	size_t refcount_;
	pthread_t worker_; //!< ワーカースレッド
	bool running_; //!< ワーカー動作
	bool worker_done_; //!< ワーカースレッド終了フラグ
    };

    /**
     * @brief 全体の管理領域
     */
    struct SharedRegion {
	uint32_t initialized_;
	pthread_mutex_t mutex_;

	proAtom protbl_[C_FJNT_PROCESS_MAX];
	size_t proptr_;
        mailAtom listen_[C_FJNT_LISTEN_MAX];
	size_t lptr_;
        mailAtom queue_[C_FJNT_QUEUE_MAX];
	size_t qptr_;
    };

    /**
     * @brief コンストラクタ
     * @param[in] shm_name 共有メモリの名前。必ず'/'で始めること。
     * @param[in] extra_size 拡張領域のサイズ(初期化後get()で取得可能)
     */
    FJSharedMem(const std::string& shm_name, size_t extra_size, const char *srcfunc = nullptr, std::vector<fjt_msg_t> list = {})
        : pid_(getpid()), shm_name_(shm_name), shared_ptr_(nullptr), user_ptr_(nullptr), extra_size_(extra_size), needworker_(false) 
    {
	if (srcfunc) {
	    srcfunc_ = srcfunc;
	}
        bool is_create = false;
        int fd = shm_open(C_FJNT_SHAREDREGION_NAME, O_RDWR, 0666);
	if (fd < 0) {
	    // まだ存在しない
            fd = shm_open(C_FJNT_SHAREDREGION_NAME, O_CREAT | O_RDWR, 0666);
            if (fd < 0) { perror("shm_open"); exit(1); }
            if (ftruncate(fd, sizeof(SharedRegion)) < 0) { perror("shm_ftruncate"); exit(1); }
	    is_create = true;
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): created sharedmem[" << C_FJNT_SHAREDREGION_NAME << "]" << COLOR_RESET << std::endl;
#endif
	}
	// mapはmaster/slave共通
        shared_ptr_ = mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (shared_ptr_ == MAP_FAILED) { perror("shm_mmap"); exit(1); }
        shared_region_ = reinterpret_cast<SharedRegion*>(shared_ptr_);
	close(fd);

	// 個別の領域
        fd = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (fd < 0) {
	    // まだ存在しない
            fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
            if (fd < 0) { perror("ext_open"); exit(1); }
            if (ftruncate(fd, extra_size_) < 0) { perror("ext_ftruncate"); exit(1); }
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): created extmem[" << shm_name_ << "]" << COLOR_RESET << std::endl;
#endif
        }

	// mapはmaster/slave共通
        user_ptr_ = mmap(nullptr, extra_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (user_ptr_ == MAP_FAILED) { perror("ext_mmap"); exit(1); }
	close(fd);

        if (is_create) {
	    // 全プロセスで初回のみ
            initSharedRegion();
	} else {
	    // slaveの場合初期化が終わるまでspinlock
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
	// 通知の登録
	int regi = _addListen(this, list);
	if (regi > 0) {
	    // 通知が必要なインスタンスの場合ワーカーが必要
	    needworker_ = true;
	    FJFixMap<proAtom> protbl((char*)&(shared_region_->protbl_), sizeof(proAtom)*C_FJNT_PROCESS_MAX, shared_region_->proptr_);
	    proAtom *p = protbl.find(pid_);
	    if (p == nullptr) {
		// 新規
		p = protbl.insert(pid_);
		if (p != nullptr) {
		    _proAtomInit(p);
		}
	    }

	    p->refcount_ = p->refcount_ + 1; //!< 1プロセスあたりの参照カウント+1
	    if (p->refcount_ == 1) {
		// このプロセスで初回のみスレッド起動
		p->running_ = true;
		p->worker_done_ = false;
		pthread_create(&p->worker_, nullptr, &FJSharedMem::workerThreadWrapper, this);
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << "): created worker thread" << COLOR_RESET << std::endl;
#endif
	    }
	}
	++(shared_region_->initialized_); //!< 全プロセス中の合計インスタンス数++

	pthread_mutex_unlock(&shared_region_->mutex_);
    }

    /**
     * @brief デストラクタ
     */
    virtual ~FJSharedMem() {
        if (user_ptr_) {
	    bool is_unlink = false;
	    pthread_mutex_lock(&shared_region_->mutex_);
	    if (needworker_) {
		FJFixMap<proAtom> protbl((char*)&(shared_region_->protbl_), sizeof(proAtom)*C_FJNT_PROCESS_MAX, shared_region_->proptr_);
		proAtom *p = protbl.find(pid_);
		if (p != nullptr) {
		    p->refcount_ = p->refcount_ - 1; //!< 1プロセス中の参照カウント--
		    if (p->refcount_ == 0) {
			p->running_ = false;
			pthread_mutex_lock(&p->mutex_);
			pthread_cond_broadcast(&p->cond_);
			pthread_mutex_unlock(&p->mutex_);

			// スレッド終了までspinlock
			static uint32_t timeoutMs = 100;
			uint32_t i = 0;
			do {
			    pthread_mutex_lock(&p->mutex_);
			    if (p->worker_done_ == true) {
				pthread_join(p->worker_, NULL);
				pthread_mutex_unlock(&p->mutex_);
				break;
			    }
			    usleep(1000);
			} while (i++ < timeoutMs);
			if (i == timeoutMs) {
			    std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << "): thread join timeout." << COLOR_RESET << std::endl;
			}
			_proAtomDestroy(p);
			protbl.unset(pid_); // 削除
		    }
		}
	    }
	    pthread_mutex_unlock(&shared_region_->mutex_);
	    
	    usleep(0);

	    pthread_mutex_lock(&shared_region_->mutex_);
	    if (needworker_) {
		FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
		for (int32_t i = 0; i < shared_region_->qptr_; ++i) {
		    mailAtom *m = queue.at(i);
		    if (m->obj_ == this) {
			_mailAtomInit(m);
		    }
		}
		queue.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
		while (queue.size() > 0 && queue.at(0)->msg_ == 0) {
		    queue.splice(0, 1);
		}

		FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
		for (int32_t i = 0; i < shared_region_->lptr_; ++i) {
		    mailAtom *m = listen.at(i);
		    if (m->obj_ == this) {
			_mailAtomInit(m);
		    }
		}
		listen.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
		while (listen.size() > 0 && listen.at(0)->msg_ == 0) {
		    listen.splice(0, 1);
		}
	    }
	    --(shared_region_->initialized_); //!< 全プロセス中の合計インスタンス数--
	    if (shared_region_->initialized_ == 0) {
		is_unlink = true;
	    }
	    pthread_mutex_unlock(&shared_region_->mutex_);

	    munmap(user_ptr_, extra_size_);
	    munmap(shared_ptr_, sizeof(SharedRegion));
	    if (is_unlink) {
		shm_unlink(shm_name_.c_str());
		shm_unlink(C_FJNT_SHAREDREGION_NAME);
	    }
	}
    }

    /**
     * @brief 拡張領域のポインタ
     * @note void型であるため継承先のキャストが必要
     */
    void *_get() {
	void *ptr = nullptr;
	pthread_mutex_lock(&shared_region_->mutex_);
	ptr = user_ptr_;
	pthread_mutex_unlock(&shared_region_->mutex_);
	return ptr;
    }

    /**
     * @brief 受け取りたい通知を設定(スカラー)
     * @param[in] obj リスナーのオブジェクト(通常は自分自身)
     * @param[in] msg メッセージID
     */
    bool addListen(FJSharedMem* obj, fjt_msg_t msg) {
        if (!obj || !msg) return false;

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
        if (!from) return false;

	uint32_t msgcount = 0;
	std::map<uint32_t,uint32_t> pids;

	FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
	FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);

	// lower bound algo.
	int start = -1;
	size_t left = 0, right = shared_region_->lptr_;
	while (left < right) {
	    size_t mid = left + (right - left) / 2;
	    mailAtom *m = listen.at(mid);
	    if (m->msg_ < msg) {
		left = mid + 1;
	    } else {
		right = mid;
	    }
	}
	if (left < shared_region_->lptr_ && listen.at(left)->msg_ == msg) {
	    start = left;
	}
	if (start == -1) {
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_GRAY << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << from << "): msg[" << msg << "] is invalid" << COLOR_RESET << std::endl;
#endif
	    return false;
	}

	// lesten_をstartからlptr_までループするfor文
	for (uint32_t i = start; i < shared_region_->lptr_; ++i) {
	    mailAtom *to = listen.at(i);
	    if (to->msg_ != msg) { //!< 異なるIDが現れたら抜ける
		break;
	    }
	    if (to->obj_ == from) { //!< 自分自身にはメッセージ送らない
		continue;
	    }
#if FJSHAREDMEM_DBG == 1
	    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << from << "): send msg[" << msg << "] to:" << to->obj_ << COLOR_RESET << std::endl;
#endif
	    if (queue.push_back(*to) == true) {
		++msgcount;
		pids[to->pid_] = 1;
	    } else {
		std::cerr << COLOR_RED << "ERROR: " << srcfunc_ << "(pid:" << pid_ << " obj:" << from << "): msg[" << msg << "] to:" << to->obj_ << " queue is full." << COLOR_RESET << std::endl;
	    }
	}
	// ブロードキャスト
	if (msgcount > 0) {
	    FJFixMap<proAtom> protbl((char*)&(shared_region_->protbl_), sizeof(proAtom)*C_FJNT_PROCESS_MAX, shared_region_->proptr_);
	    std::map<uint32_t,uint32_t>::iterator it;
	    for (it = pids.begin(); it != pids.end(); ++it) {
		proAtom *p = protbl.find(it->first);
		if (p != nullptr && p->running_ == true && p->worker_done_ == false) {
		    pthread_mutex_lock(&p->mutex_);
		    pthread_cond_broadcast(&p->cond_);
		    pthread_mutex_unlock(&p->mutex_);
		}
	    }
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
     * @brief mailAtom初期化
     * @param[in] m ポインタ
     */
    void _mailAtomInit(struct mailAtom* m) {
	m->msg_ = 0;
	m->obj_ = nullptr;
	m->pid_ = 0;
    }

    /**
     * @brief proAtom初期化
     * @param[in] p ポインタ
     */
    void _proAtomInit(struct proAtom *p) {
        pthread_mutexattr_t mattr;
        pthread_condattr_t cattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

	// 1プロセスで共有する排他
        pthread_mutex_init(&p->mutex_, &mattr);
	// 1プロセスで共有する状態変数
        pthread_cond_init(&p->cond_, &cattr);
	// ワーカースレッド関係初期化
	p->refcount_ = 0;
	p->worker_ = 0;
	p->running_ = true;
	p->worker_done_ = false;
    };

    void _proAtomDestroy(struct proAtom *p) {
	pthread_mutex_destroy(&p->mutex_);
	pthread_cond_destroy(&p->cond_);
    }

    /**
     * @brief 管理領域初期化
     */
    void initSharedRegion() {
        static bool initialized = false;
        if (initialized) return;
        initialized = true;

	// 全プロセスで共有する排他
        pthread_mutexattr_t mattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shared_region_->mutex_, &mattr);
	// プロセス管理テーブル初期化
	shared_region_->proptr_ = 0;
	// Listen配列の初期化
        memset(shared_region_->listen_, 0, sizeof(shared_region_->listen_));
        shared_region_->lptr_ = 0;
	// queue配列の初期化
        memset(shared_region_->queue_, 0, sizeof(shared_region_->queue_));
        shared_region_->qptr_ = 0;
	// 全プロセスのインスタンス数の初期化
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
		if (listen.push_back(add) == true) {
		    regi++;
#if FJSHAREDMEM_DBG == 1
		    std::cerr << COLOR_CYAN << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): msg["<< msg << "] registered." << COLOR_RESET << std::endl;
#endif
		} else {
#if FJSHAREDMEM_DBG == 1
		    std::cerr << COLOR_RED << "WARNING: " << srcfunc_ << "(pid:" << pid_ << " obj:" << obj << "): msg["<< msg << "] not registered." << COLOR_RESET << std::endl;
#endif
		}
	    }
	}
	listen.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
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
	pthread_setname_np(pthread_self(), "shm_work");

	pthread_mutex_lock(&shared_region_->mutex_);
	FJFixMap<proAtom> protbl((char*)&(shared_region_->protbl_), sizeof(proAtom)*C_FJNT_PROCESS_MAX, shared_region_->proptr_);
	proAtom *p = protbl.find(pid_);
	pthread_mutex_unlock(&shared_region_->mutex_);

        while (p != nullptr && p->running_) {
            pthread_mutex_lock(&p->mutex_);
            pthread_cond_wait(&p->cond_, &p->mutex_);
	    if (p->running_ == false) {
		pthread_mutex_unlock(&p->mutex_);
		break;
	    }
	    pthread_mutex_unlock(&p->mutex_);

	    pthread_mutex_lock(&shared_region_->mutex_);
	    FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
            std::vector<mailAtom> locals;
	    // queue_を0からqptr_までループするfor文
	    for (int32_t i = 0; i < shared_region_->qptr_; ++i) {
		mailAtom *to = queue.at(i);
		if (to->msg_ != 0 && to->obj_ != nullptr && to->pid_ == pid_) {
		    // 自分のプロセス内のメッセージに限る
		    locals.push_back(*to);
		    _mailAtomInit(to);
		}
	    }
	    queue.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
	    while (queue.size() > 0 && queue.at(0)->msg_ == 0) {
		queue.splice(0, 1);
	    }
            pthread_mutex_unlock(&shared_region_->mutex_);

            for (auto& mail : locals) {
#if FJSHAREDMEM_DBG == 1
		std::cerr << COLOR_YELLOW << "INFO: " << srcfunc_ << "(pid:" << pid_ << " obj:" << this << "): received msg[" << mail.msg_ << "] from " << mail.obj_ << COLOR_RESET << std::endl;
#endif
                if (mail.obj_) mail.obj_->update(mail.obj_, mail.msg_);
#if FJSHAREDMEM_DBG == 1
		std::cerr << "OK[" << mail.msg_ << "]" << std::endl;
#endif
            }

        }
#if FJSHAREDMEM_DBG == 1
	std::cerr << COLOR_RED << "pid:" << pid_ << "'s workerThread ended." << COLOR_RESET << std::endl;
#endif	
	pthread_mutex_lock(&p->mutex_);
	p->worker_done_ = true;
	pthread_mutex_unlock(&p->mutex_);
    }

    std::string shm_name_; //!< 共有メモリの名前
    std::string srcfunc_; //!< 共有メモリを作成した関数名
    void* shared_ptr_; //!< 管理領域の先頭
    SharedRegion* shared_region_; //!< 管理構造体
    size_t extra_size_; //!< 拡張サイズ
    bool needworker_; ////!< ワーカースレッド必要フラグ
};

#endif //__FJSHAREDMEM_H__

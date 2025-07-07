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
#include <thread>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fjfixvector.h"

#define C_FJNT_LISTEN_MAX 256 //!< リスナーテーブル最大数
#define C_FJNT_QUEUE_MAX 512 //!< メッセージキュー最大数

typedef uint32_t fjt_msg_t; //!< メッセージID型

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
    FJSharedMem(const std::string& shm_name, size_t extra_size)
        : pid_(getpid()), shm_name_(shm_name), full_ptr_(nullptr), user_ptr_(nullptr)
    {
        total_size_ = sizeof(SharedRegion) + extra_size;
        bool is_create = false;
        int fd = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (fd < 0) {
			std::cerr << "shm_open failed: " << shm_name_ << ":" << strerror(errno) << " (errno=" << errno << ")\n";
			// まだ存在しない
            fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
            if (fd < 0) { perror("shm_open"); exit(1); }
            if (ftruncate(fd, total_size_) < 0) { perror("ftruncate"); exit(1); }
			is_create = true;
        }

        full_ptr_ = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (full_ptr_ == MAP_FAILED) { perror("mmap"); exit(1); }
		close(fd);

        shared_region_ = reinterpret_cast<SharedRegion*>(full_ptr_);
        user_ptr_ = reinterpret_cast<void*>((char*)full_ptr_ + sizeof(SharedRegion));
        if (is_create) {
            initSharedRegion();
		} else {
			for (uint32_t i = 0; i < 100 && shared_region_->initialized_ == 0; i++) {
				usleep(1000);
			}
		}
        static bool is_first = true;
        if (is_first) {
           is_first = false;
		   p_refcount_ = 1;
		   worker_ = std::thread([this] { this->workerThread(); });
		}
		pthread_mutex_lock(&shared_region_->mutex_);
		++(shared_region_->initialized_);
		pthread_mutex_unlock(&shared_region_->mutex_);
    }

	/**
	 * @brief デストラクタ
	 */
    virtual ~FJSharedMem() {
        if (full_ptr_) {
			bool is_unlink = false;
			if (shared_region_->initialized_ > 0) {
				pthread_mutex_lock(&shared_region_->mutex_);
				pthread_cond_broadcast(&shared_region_->cond_);
				--p_refcount_;
				--(shared_region_->initialized_);
				if (shared_region_->initialized_ == 0) {
					is_unlink = true;
				}

				FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
				for (uint32_t i = 0; i < shared_region_->qptr_; ++i) {
					mailAtom *m = queue.at(i);
					if (m->obj_ == this) {
						queue.splice(i, 1);
						shared_region_->qptr_ = queue.length();
						i = 0;
						continue;
					}
				}
				FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
				for (uint32_t i = 0; i < shared_region_->lptr_; ++i) {
					mailAtom *l = listen.at(i);
					if (l->obj_ == this) {
						queue.splice(i, 1);
						shared_region_->lptr_ = listen.length();
						i = 0;
						continue;
					}
				}
				pthread_mutex_unlock(&shared_region_->mutex_);
				if (p_refcount_ == 0 && worker_.joinable()) {
					worker_.join();
				}
			}
			munmap(full_ptr_, total_size_);
			if (is_unlink) {
				shm_unlink(shm_name_.c_str());
			}
		}
    }

	/**
	 * @brief 拡張領域のポインタ
	 */
	void *get() {
		void *ptr = nullptr;
		if (p_refcount_ > 0) {
			pthread_mutex_lock(&shared_region_->mutex_);
			ptr = user_ptr_;
			pthread_mutex_unlock(&shared_region_->mutex_);
		}
		return ptr;
	}

	/**
	 * @brief 受け取りたい通知を設定
	 * @note 重複チェックは行わないため注意。
	 * @param[in] obj リスナーのオブジェクト(通常は自分自身)
	 * @param[in] msg メッセージID
	 */
    bool addListen(FJSharedMem* obj, fjt_msg_t msg) {
        if (!obj || msg == 0) return false;
        pthread_mutex_lock(&shared_region_->mutex_);
		
		FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
		mailAtom add = { msg, obj, pid_ };
		listen.push_back(add);
		listen.sort([](const mailAtom& a, const mailAtom& b) { return a.msg_ < b.msg_; });		
		shared_region_->lptr_ = listen.length();
        pthread_mutex_unlock(&shared_region_->mutex_);
        return true;
    }

	/**
	 * @brief 通知を送信
	 * @param[in] obj センダーのオブジェクト(通常は自分自身)
	 * @param[in] msg メッセージID
	 */
    bool notify(FJSharedMem* obj, fjt_msg_t msg) {
        if (!obj || msg == 0) return false;
        pthread_mutex_lock(&shared_region_->mutex_);

		FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);
		FJFixVector<mailAtom> listen((char*)&(shared_region_->listen_), sizeof(mailAtom)*C_FJNT_LISTEN_MAX, shared_region_->lptr_);
		// lesten_を0からlptr_までループするfor文
		for (uint32_t i = 0; i < shared_region_->lptr_; ++i) {
			mailAtom *l = listen.at(i);
			// 自分自身にはメッセージ送らない
			if (l->msg_ == msg && l->obj_ != obj) {
				mailAtom n;
				n.msg_ = l->msg_;
				n.obj_ = l->obj_;
				n.pid_ = l->pid_;
				if (queue.push_back(n) == false) {
					perror("queue"); exit(1);
				}
 				shared_region_->qptr_ = queue.length();
			}
		}
		// ブロードキャスト
        pthread_cond_broadcast(&shared_region_->cond_);
        pthread_mutex_unlock(&shared_region_->mutex_);
        return true;
    }

	/**
	 * @brief 通知を受け取る関数
	 */
    virtual void update(FJSharedMem* obj, fjt_msg_t msg) {};

protected:
    void* user_ptr_; //!< 拡張領域のポインタ
    pid_t pid_; //!< プロセスID

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
	 * @brief ワーカースレッド
	 * @note 1プロセスにつき1つ
	 */
    void workerThread() {
        while (p_refcount_ > 0) {
            pthread_mutex_lock(&shared_region_->mutex_);
            pthread_cond_wait(&shared_region_->cond_, &shared_region_->mutex_);

			FJFixVector<mailAtom> queue((char*)&(shared_region_->queue_), sizeof(mailAtom)*C_FJNT_QUEUE_MAX, shared_region_->qptr_);

            std::vector<mailAtom> locals;
			for (uint32_t i = 0; i < shared_region_->qptr_; ++i) {
				// queue_を0からqptr_までループするfor文
				mailAtom *m = queue.at(i);
				if (m->msg_ != 0 && m->obj_ != nullptr && m->pid_ == pid_) {
					// 自分のプロセス内のメッセージに限る
					mailAtom o;
					o.msg_ = m->msg_;
					o.obj_ = m->obj_;
					o.pid_ = m->pid_;
					locals.push_back(o);
					// 左詰め
					queue.splice(i, 1);
					shared_region_->qptr_ = queue.length();
					i = 0;
					continue;
				}
			}
            pthread_mutex_unlock(&shared_region_->mutex_);

            for (auto& mail : locals) {
                if (mail.obj_) mail.obj_->update(mail.obj_, mail.msg_);
            }
        }
    }

    std::string shm_name_; //!< 共有メモリの名前
    void* full_ptr_; //!< 共有メモリの先頭
    SharedRegion* shared_region_; //!< 管理構造体
    size_t total_size_; //!< 総サイズ
	std::thread worker_; //!< ワーカースレッド

	static int p_refcount_; //!< プロセス内でのFJSharedMemインスタンスの数
};

#endif //__FJSHAREDMEM_H__

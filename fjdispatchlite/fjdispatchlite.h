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
 * @file fjdispatchlite.h
 * @author FJD
 * @date 2025.7.21
 */
#ifndef __FJDISPATCHLITE_H__
#define __FJDISPATCHLITE_H__

#ifndef DOXYGEN_SKIP_THIS
#include <iostream>
#include <queue>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cstring>
#include <future>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "fjtypes.h"
#include "fjunitframes.h"

#define FJDISPATCHLITE_DEFAULT_THREADS (2) //!< ワーカースレッド数初期値
#define FJDISPATCHLITE_MAX_THREADS (8) //!< ワーカースレッド数最大値
#define FJDISPATCHLITE_MIN_THREADS (1) //!< ワーカースレッド数最小値
#define FJDISPATCHLITE_MAX_RESULTS (100) //!< リザルトキューの最大値
#define FJDISPATCHLITE_IDLE_TIMEOUT_MSEC (60000)  //!< スレッドをシュリンクするタイムアウト値
#define FJDISPATCHLITE_HUNG_TIMEOUT_MSEC (15000)   //!< タスクが固まった判定タイムアウト値

#define FJDISPATCHLITE_DBG (0) //!< デバッグフラグ
#define FJDISPATCHLITE_PROFILE_DBG (0) //!< メソッド実行プロファイラ
#define FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC (200) //!< postQueueしてから実行されるまでの遅延許容値(msec)
#define FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC (200) //!< メソッド実行にかかる時間の許容値(msec)
#define FJDISPATCHLITE_PROFILE_MONITOR_IVAL_MSEC (5000) 


// 前方参照
class FJTimerLite;

/**
 * @brief 最小限のディスパッチャ
 * @note FJUnitFramesのメソッドの実行を受け取り、ワーカースレッドで非同期に処理する。
 */
class FJDispatchLite {
public:
    friend class FJTimerLite;

    /**
     * @brief 各ハンドルごとの実行結果
     */
    struct ResultItem {
	int value; //!< タスクの返り値
        bool ready; //!< 実行結果を受け取ったか
    };

    /*
     * @brief シングルトン
     */
    static FJDispatchLite* GetInstance() {
        static FJDispatchLite instance;
        return &instance;
    }

    /**
     * @brief デストラクタ
     */
    ~FJDispatchLite() {
        {
	    pthread_mutex_lock(&mutex_);
            stop_ = true;
	    pthread_cond_broadcast(&cv_);
	    pthread_mutex_unlock(&mutex_);
        }
        pthread_join(monitor_thread_, nullptr);
        for (auto& t : workers_) {
	    pthread_join(t.thread, nullptr);
        }
        pthread_mutex_destroy(&mutex_);
        pthread_cond_destroy(&cv_);
        pthread_mutex_destroy(&result_mutex_);
        pthread_cond_destroy(&result_cv_);
    }

    /**
     * @brief 結果アイテムの生成
     * @param[out] handle ハンドル
     * @param[out] result 結果アイテム
     */
    void _new_resultitem( fjt_handle_t &handle, std::shared_ptr<ResultItem> &result )
    {
	handle = getHandle();
	{
	    pthread_mutex_lock(&result_mutex_);
	    // 結果アイテムの登録と古い結果をキューから削除
	    results_[handle] = result;
	    result_order_.push_back(handle);
	    if (result_order_.size() > FJDISPATCHLITE_MAX_RESULTS) {
		fjt_handle_t old = result_order_.front();
		result_order_.pop_front();
		results_.erase(old);
	    }
	    pthread_cond_broadcast(&result_cv_);
	    pthread_mutex_unlock(&result_mutex_);
	}
    }

    /**
     * @brief 結果の登録
     * @param[in] handle ハンドル
     * @param[in] value 結果
     */
    void _post_resultitem( fjt_handle_t handle, int value)
    {
        pthread_mutex_lock(&result_mutex_);
	auto it = results_.find(handle);
	if (it != results_.end()) {
	    it->second->value = value;
	    it->second->ready = true;
	}
        pthread_mutex_unlock(&result_mutex_);
    }

    /**
     * @brief キューにタスクを積む
     * @note 本クラスから呼び出されるFJUnitFramesの生存期間はユーザーが保証すること。
     * @param[in] obj FJUnitFramesのポインタ
     * @param[in] mf FJUnitFramesのメソッド
     * @param[in] msg メッセージID
     * @param[in] buf データ
     * @param[in] len データバイト長
     * @param[in] isseq [true]:obj単位でシーケンシャルに実行, [false]:パラレル実行(メソッド間の資源排他を行うこと)
     * @param[in] srcfunc デバッグ表示用呼び出し関数名
     * @param[in] srcline デバッグ表示用呼び出し行数
     * @return ハンドル 
    */
    template <typename T>
    fjt_handle_t postQueue(T* obj, int (T::*mf)(uint32_t, void*, uint32_t), uint32_t msg, void* buf, uint32_t len, bool isseq, std::string srcfunc, uint32_t srcline) {
	static_assert(std::is_base_of<FJUnitFrames, T>::value, "T must derive from FJUnitFrames");
	// start_time
	auto start = _get_time();
        // bufをコピー
        char* buf_copy = new char[len];
        std::memcpy(buf_copy, static_cast<char *>(buf), len);
	// srcfuncをコピー
	char *srcfunc_copy = new char[srcfunc.size()+1];
	std::strcpy(srcfunc_copy, srcfunc.c_str());
	// ResultItem
	fjt_handle_t handle;
	auto result = std::make_shared<ResultItem>();
	_new_resultitem( handle, result );
#if FJDISPATCHLITE_DBG == 1
	{
	    std::cerr << COLOR_CYAN << "[" << start << "]:" << srcfunc  << COLOR_RESET << std::endl;
	}
#endif
	// lambda式でタスクを定義
        auto lambda = [=]() {
	    auto delay = _get_time();
            pthread_mutex_lock(&mutex_);
            for (auto& w : workers_) {
                if (pthread_self() == w.thread) {
                    w.task_start_ms = delay;
                    w.task_srcfunc = srcfunc_copy;
                }
            }
            pthread_mutex_unlock(&mutex_);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto elapsed1 = delay - start;	    
	    if (elapsed1 > FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC) {
		std::cerr << COLOR_RED << "[" << delay << "]:" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution is DELAYED. " << elapsed1 << " msec." << COLOR_RESET << std::endl;
	    }
#endif
	    int ret = (obj->*mf)(msg, buf_copy, len);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto now = _get_time();
	    auto elapsed2 = now - start;
	    if (elapsed2 > FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC) {
		std::cerr << COLOR_RED << "[" << now << "]" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution time is TOO LONG. " << elapsed2 << " msec." << COLOR_RESET << std::endl;
	    }
#endif

	    delete[] srcfunc_copy;
            delete[] buf_copy;

	    // 結果の登録
	    _post_resultitem(handle, ret);
        }; 

	auto task = std::make_unique<std::packaged_task<void()>>(lambda);

	// インスタンスのタスクキューに所有権を移動
	{
	    pthread_mutex_lock(&mutex_);
	    auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
	    inst_info.task_queue.push(std::move(task));
	    // インスタンスのタスクキューが実行中でないか、パラで動作させるフラグが立っていたら
	    if (!inst_info.running || !isseq) {
		// 実行待ちタスクに登録して実行中に
		ready_instances_.push(static_cast<FJUnitFrames*>(obj));
		inst_info.running = true;
		// イベント送信
		pthread_cond_signal(&cv_);
	    }
	    // ワーカースレッドを必要に応じて拡張
            _adjust_workers();
	    pthread_mutex_unlock(&mutex_);
         }

	return handle;
    }

    /**
     * @brief キューにイベントを積む
     * @note 本クラスから呼び出されるFJUnitFramesの生存期間はユーザーが保証すること。
     * @param[in] obj FJUnitFramesのポインタ
     * @param[in] mf FJUnitFramesのメソッド
     * @param[in] msg メッセージID
     * @param[in] srcfunc デバッグ表示用呼び出し関数名
     * @param[in] srcline デバッグ表示用呼び出し行数
     * @return ハンドル 
    */
    template <typename T>
    fjt_handle_t postEvent(T* obj, int (T::*mf)(uint32_t), uint32_t msg, std::string srcfunc, uint32_t srcline) {
	static_assert(std::is_base_of<FJUnitFrames, T>::value, "T must derive from FJUnitFrames");
	// start_time
	auto start = _get_time();
	// srcfuncをコピー
	char *srcfunc_copy = new char[srcfunc.size()+1];
	std::strcpy(srcfunc_copy, srcfunc.c_str());
	// ResultItem
	fjt_handle_t handle;
	auto result = std::make_shared<ResultItem>();
	_new_resultitem( handle, result );
#if FJDISPATCHLITE_DBG == 1
	{
	    std::cerr << COLOR_CYAN << "[" << start << "]:" << srcfunc  << COLOR_RESET << std::endl;
	}
#endif
	// lambda式でタスクを定義
        auto lambda = [=]() {
	    auto delay = _get_time();
            pthread_mutex_lock(&mutex_);
            for (auto& w : workers_) {
                if (pthread_self() == w.thread) {
                    w.task_start_ms = delay;
                    w.task_srcfunc = srcfunc_copy;
                }
            }
            pthread_mutex_unlock(&mutex_);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto elapsed1 = delay - start;	    
	    if (elapsed1 > FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC) {
		std::cerr << COLOR_RED << "[" << delay << "]:" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution is DELAYED. " << elapsed1 << " msec." << COLOR_RESET << std::endl;
	    }
#endif
	    int ret = (obj->*mf)(msg);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto now = _get_time();
	    auto elapsed2 = now - start;
	    if (elapsed2 > FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC) {
		std::cerr << COLOR_RED << "[" << now << "]" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution time is TOO LONG. " << elapsed2 << " msec." << COLOR_RESET << std::endl;
	    }
#endif

	    delete[] srcfunc_copy;

	    // 結果の登録
	    _post_resultitem(handle, ret);
        };

	auto task = std::make_unique<std::packaged_task<void()>>(lambda);

	// インスタンスのタスクキューに所有権を移動
	{
	    pthread_mutex_lock(&mutex_);
	    auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
	    inst_info.task_queue.push(std::move(task));
	    // インスタンスのタスクキューが実行中でないか
	    if (!inst_info.running) {
		// 実行待ちタスクに登録して実行中に
		ready_instances_.push(static_cast<FJUnitFrames*>(obj));
		inst_info.running = true;
		// イベント送信
		pthread_cond_signal(&cv_);
	    }
	    // ワーカースレッドを必要に応じて拡張
            _adjust_workers();
	    pthread_mutex_unlock(&mutex_);
        }

	return handle;
    }

    /**
     * @brief キューにタスクを積む
     * @note 本クラスから呼び出されるFJUnitFramesの生存期間はユーザーが保証すること。
     * @param[in] obj FJUnitFramesのポインタ
     * @param[in] task std::packaged_task
     */
    template <typename T>
    fjt_handle_t enqueueTask(T* obj, std::packaged_task<void()>&& task) {
	static_assert(std::is_base_of<FJUnitFrames, T>::value, "T must derive from FJUnitFrames");
	fjt_handle_t handle = getHandle();

	// インスタンスのタスクキューに所有権を移動
	{
	    pthread_mutex_lock(&mutex_);
	    auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
	    auto task_ptr = std::make_unique<std::packaged_task<void()>>(std::move(task));
	    inst_info.task_queue.push(std::move(task_ptr));
	    // インスタンスのタスクキューが実行中でないか
	    if (!inst_info.running) {
		// 実行待ちタスクに登録して実行中に
		ready_instances_.push(static_cast<FJUnitFrames*>(obj));
		inst_info.running = true;
		// イベント送信
		pthread_cond_signal(&cv_);
	    }
	    // ワーカースレッドを必要に応じて拡張
	    _adjust_workers();
	    pthread_mutex_unlock(&mutex_);
        }

	return handle;
    }

    /**
     * @brief タスクの実行結果を待つ
     * @param[in] handle 待受ハンドル
     * @param[in] timeout_msec 最大待ち時間(msec)
     * @paaram[out] result_out タスクの返り値
     * @retval [true] 実行結果が取得できた
     * @retval [false] 待受ハンドルの実行結果が見つからない
     */
    bool waitResult(fjt_handle_t handle, uint32_t timeout_msec, int& result_out) {
	auto start = _get_time();
        pthread_mutex_lock(&result_mutex_);

        while (true) {
            auto it = results_.find(handle);
            if (it != results_.end()) {
		// テーブルに存在し
                auto& item = it->second;
                if (item->ready) {
		    // 実行完了している
                    result_out = item->value;
                    pthread_mutex_unlock(&result_mutex_);
                    return true;
		}
	    }		
	    auto now = _get_time();
	    auto elapsed = now - start;
	    if (elapsed >= timeout_msec) {
		pthread_mutex_unlock(&result_mutex_);
		return false;
	    }
	    struct timespec next;
	    _get_future_timespec(&next, elapsed > 33 ? 33 : elapsed);
	    pthread_cond_timedwait(&result_cv_, &result_mutex_, &next);
        }
    }

private:
    /**
     * @brief ワーカーの動作状況
     */
    struct WorkerInfo {
        pthread_t thread;
        uint64_t last_active_ms;
        uint64_t task_start_ms = 0;
        std::string task_srcfunc;
    };

    /**
     * @brief 各FJUintFramesごとのインスタンス情報
     */
    struct InstanceInfo {
	std::queue<std::unique_ptr<std::packaged_task<void()>>> task_queue; //!< タスクキュー
        bool running = false; //!< このインスタンスのタスクがスレッドで実行中か
    };
    
    /**
     * @brief デフォルトコンストラクタ
     */
    FJDispatchLite() : stop_(false), num_of_threads_(FJDISPATCHLITE_DEFAULT_THREADS) {
        pthread_mutex_init(&mutex_, NULL);
        pthread_cond_init(&cv_, NULL);
        pthread_mutex_init(&result_mutex_, NULL);
        pthread_cond_init(&result_cv_, NULL);
        for (int i = 0; i < num_of_threads_; ++i) _spawn_worker();
        pthread_create(&monitor_thread_, NULL, &FJDispatchLite::monitorFunc, this);
    }

    void _spawn_worker() {
        WorkerInfo info;
        info.last_active_ms = _get_time();
        pthread_create(&info.thread, NULL, &FJDispatchLite::workerFunc, this);
        workers_.push_back(info);
    }

    void _adjust_workers() {
        if (ready_instances_.size() > num_of_threads_ && num_of_threads_ < FJDISPATCHLITE_MAX_THREADS) {
            _spawn_worker();
            ++num_of_threads_;
#if FJDISPATCHLITE_DBG != 0
	    std::cerr << COLOR_RED << "*WARNING* worker threads++ (" << num_of_threads_ << ")" << COLOR_RESET << std::endl;
#endif
        }
    }
    
    void _shrink_workers() {
        auto now = _get_time();
        for (auto it = workers_.begin(); it != workers_.end();) {
            if (workers_.size() <= FJDISPATCHLITE_MIN_THREADS) break;
            if (now - it->last_active_ms >= FJDISPATCHLITE_IDLE_TIMEOUT_MSEC) {
                pthread_cancel(it->thread);
                pthread_join(it->thread, nullptr);
                it = workers_.erase(it);
                --num_of_threads_;
            } else {
                ++it;
            }
        }
    }

   /**
     * @brief コピー禁止コンストラクタ
     */
    FJDispatchLite(const FJDispatchLite&) = delete;

    /**
     * @brief コピー禁止コンストラクタ
     */
    FJDispatchLite& operator=(const FJDispatchLite&) = delete;

    /**
     * @brief 新しいハンドルを確保
     * @return 新しいハンドル
     */
    fjt_handle_t getHandle() {
	pthread_mutex_lock(&mutex_);
	fjt_handle_t handle = ++handle_counter_;
	if (handle >= INT64_MAX) handle_counter_ = 1;
	pthread_mutex_unlock(&mutex_);
	return handle;
    }

    static void* workerFunc(void* arg) {
        static_cast<FJDispatchLite*>(arg)->workerThread();
        return nullptr;
    }

    static void* monitorFunc(void* arg) {
        FJDispatchLite* self = static_cast<FJDispatchLite*>(arg);
        while (!self->stop_) {
            pthread_mutex_lock(&self->mutex_);
            auto now = _get_time();
            for (const auto& w : self->workers_) {
                if (!w.task_srcfunc.empty() && (now - w.task_start_ms >= FJDISPATCHLITE_HUNG_TIMEOUT_MSEC)) {
                    std::cerr << COLOR_YELLOW << "[MONITOR] Hung task: " << w.task_srcfunc << " (" << (now - w.task_start_ms) << "ms)" << COLOR_RESET << std::endl;
                }
            }
            pthread_mutex_unlock(&self->mutex_);
	    usleep(FJDISPATCHLITE_PROFILE_MONITOR_IVAL_MSEC * 1000);
        }
        return nullptr;
    }

    /**
     * @brief ワーカースレッドの実装
     */
    void workerThread() {
        while (true) {
            FJUnitFrames* inst = nullptr;
	    std::unique_ptr<std::packaged_task<void()>> task;

	    pthread_mutex_lock(&mutex_);
	    // 終了宣言済みか、または、実行待ちタスクがあるとき抜ける
	    while (!stop_ && ready_instances_.empty()) pthread_cond_wait(&cv_, &mutex_);
	    if (stop_) {
		pthread_mutex_unlock(&mutex_);
		break;
	    }
	    // インスタンスをpop
	    inst = ready_instances_.front();
	    ready_instances_.pop();
	    auto& inst_info = instance_map_[inst];
	    if (inst_info.task_queue.empty()) {
		// インスタンスのタスクキューが空ならば止める
		inst_info.running = false;
		pthread_mutex_unlock(&mutex_);
		continue;
	    }
	    // タスクの所有権をタスクキューからこのコンテキストに移動
	    task = std::move(inst_info.task_queue.front());
	    inst_info.task_queue.pop();
	    pthread_mutex_unlock(&mutex_);

            // タスク実行(排他範囲外にしておくこと)
            (*task)();

	    pthread_mutex_lock(&mutex_);

	    for (auto& w : workers_) {
		if (pthread_self() == w.thread) {
		    w.last_active_ms = _get_time();
		    w.task_srcfunc.clear();
		    break;
		}
	    }
	    if (!inst_info.task_queue.empty()) {
		// まだタスクキューが空でなかったら実行待ちタスクに登録
		ready_instances_.push(inst);
		pthread_cond_signal(&cv_);
	    } else {
		// このインスタンスで処理するものがなかったら止める
		inst_info.running = false;
	    }

	    pthread_mutex_unlock(&mutex_);
	}
    }

private:
    pthread_mutex_t mutex_; //!< 排他
    pthread_cond_t cv_; //!< 状態変数
    bool stop_; //!< 終了宣言変数
    std::vector<WorkerInfo> workers_; //!< ワーカースレッド
    size_t num_of_threads_ = FJDISPATCHLITE_DEFAULT_THREADS; //!< ワーカースレッドの数

    std::unordered_map<FJUnitFrames*, InstanceInfo> instance_map_; //!< インスタンス管理テーブル
    std::queue<FJUnitFrames*> ready_instances_; //!< 実行待ちタスク

    pthread_mutex_t result_mutex_; //!< リザルト排他
    pthread_cond_t result_cv_; //!< リザルト状態変数
    std::unordered_map<fjt_handle_t, std::shared_ptr<ResultItem>> results_; //!< リザルトテーブル
    std::deque<fjt_handle_t> result_order_;  //! 順序付きでリザルト保存
    fjt_handle_t handle_counter_ = 0; //!< ハンドルカウンタ

    pthread_t monitor_thread_; //!< モニタースレッド
};

#endif //__FJDISPATCHLITE_H__


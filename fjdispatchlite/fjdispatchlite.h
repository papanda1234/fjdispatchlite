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
 * @date 2025.5.29
 */
#ifndef __FJDISPATCHLITE_H__
#define __FJDISPATCHLITE_H__

#ifndef DOXYGEN_SKIP_THIS
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cstring>
#include <future>
#include <stdint.h>
#include <time.h>
#endif

#include "fjtypes.h"
#include "fjunitframes.h"

#define FJDISPATCHLITE_DEFAULT_THREADS (6) //!< ワーカースレッド数初期値
#define FJDISPATCHLITE_MAX_THREADS (12) //!< ワーカースレッド数最大値
#define FJDISPATCHLITE_MAX_RESULTS (100) //!< リザルトキューの最大値

#define FJDISPATCHLITE_DBG (0) //!< デバッグフラグ
#define FJDISPATCHLITE_PROFILE_DBG (0) //!< メソッド実行プロファイラ
#define FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC (200) //!< postQueueしてから実行されるまでの遅延許容値(msec)
#define FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC (100) //!< メソッド実行にかかる時間の許容値(msec)

#define COLOR_RED     "\033[31m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RESET   "\033[0m"

// 前方参照
class FJTimerLite;

/**
 * @brief 最小限のディスパッチャ
 * @note FJUnitFramesのメソッドの実行を受け取り、ワーカースレッドで非同期に処理する。
 */
class FJDispatchLite {
public:
    friend class FJTimerLite;

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
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
        }
        for(auto& t : workers_) {
            if(t.joinable()) t.join();
        }
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
	auto start = std::chrono::steady_clock::now();
        // bufをコピー
        char* buf_copy = new char[len];
        std::memcpy(buf_copy, static_cast<char *>(buf), len);
	// srcfuncをコピー
	char *srcfunc_copy = new char[srcfunc.size()+1];
	std::strcpy(srcfunc_copy, srcfunc.c_str());

	auto result = std::make_shared<ResultItem>();
	fjt_handle_t handle;
	{
            std::lock_guard<std::mutex> lock(result_mutex_);
            handle = ++handle_counter_;
	    // 結果アイテムの登録と古い結果をキューから削除
            results_[handle] = result;
            result_order_.push_back(handle);
            if (result_order_.size() > FJDISPATCHLITE_MAX_RESULTS) {
                fjt_handle_t old = result_order_.front();
                result_order_.pop_front();
                results_.erase(old);
            }
            result_cv_.notify_all();
        }
#if FJDISPATCHLITE_DBG == 1
	{
	    uint32_t timeMs;
	    struct timespec ts;
	    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
	    timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
	    std::cerr << COLOR_CYAN << "[" << timeMs << "]:" << srcfunc  << COLOR_RESET << std::endl;
	}
#endif
	// lambda式でタスクを定義
	auto lambda = [obj, mf, msg, buf_copy, len, srcfunc_copy, srcline, start, result, handle, this]() {
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto delay = std::chrono::steady_clock::now();
	    auto elapsed1 = std::chrono::duration_cast<std::chrono::milliseconds>(delay - start);	    
	    if (elapsed1.count() > FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC) {
		uint32_t timeMs;
		struct timespec ts;
		int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
		timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
		std::cerr << COLOR_RED << "[" << timeMs << "]:" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution is DELAYED. " << elapsed1.count() << " msec." << COLOR_RESET << std::endl;
	    }
#endif
	    int ret = (obj->*mf)(msg, buf_copy, len);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto now = std::chrono::steady_clock::now();
	    auto elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
	    if (elapsed2.count() > FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC) {
		uint32_t timeMs;
		struct timespec ts;
		int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
		timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
		std::cerr << COLOR_RED << "[" << timeMs << "]" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution time is TOO LONG. " << elapsed2.count() << " msec." << COLOR_RESET << std::endl;
	    }
#endif

	    delete[] srcfunc_copy;
            delete[] buf_copy;

	    // 結果の登録
	    auto it = results_.find(handle);
            if (it != results_.end()) {
                it->second->value = ret;
                it->second->ready = true;
                it->second->cv.notify_all();
            }

        };

	auto task = std::make_unique<std::packaged_task<void()>>(lambda);

	// インスタンスのタスクキューに所有権を移動
        std::unique_lock<std::mutex> lock(mutex_);
	auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
        inst_info.task_queue.push(std::move(task));

	// インスタンスのタスクキューが実行中でないか、パラで動作させるフラグが立っていたら
	if (!inst_info.running || isseq == false) {
	    // 実行待ちタスクに登録して実行中に
	    ready_instances_.push(static_cast<FJUnitFrames*>(obj));
            inst_info.running = true;

	    // ワーカースレッドを必要に応じて拡張
	    if (ready_instances_.size() >= num_of_threads_ && num_of_threads_ < FJDISPATCHLITE_MAX_THREADS) {
		workers_.emplace_back(&FJDispatchLite::workerThread, this);
		++num_of_threads_;
#if FJDISPATCHLITE_DBG != 0
		std::cerr << COLOR_RED << "*WARNING* worker threads++ (" << num_of_threads_ << ")" << COLOR_RESET << std::endl;
#endif
	    }
            cv_.notify_one();
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
	auto start = std::chrono::steady_clock::now();
	// srcfuncをコピー
	char *srcfunc_copy = new char[srcfunc.size()+1];
	std::strcpy(srcfunc_copy, srcfunc.c_str());

	auto result = std::make_shared<ResultItem>();
	fjt_handle_t handle;
	{
            std::lock_guard<std::mutex> lock(result_mutex_);
            handle = ++handle_counter_;
	    // 結果アイテムの登録と古い結果をキューから削除
            results_[handle] = result;
            result_order_.push_back(handle);
            if (result_order_.size() > FJDISPATCHLITE_MAX_RESULTS) {
                fjt_handle_t old = result_order_.front();
                result_order_.pop_front();
                results_.erase(old);
            }
            result_cv_.notify_all();
        }
#if FJDISPATCHLITE_DBG == 1
	{
	    uint32_t timeMs;
	    struct timespec ts;
	    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
	    timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
	    std::cerr << COLOR_CYAN << "[" << timeMs << "]:" << srcfunc  << COLOR_RESET << std::endl;
	}
#endif
	// lambda式でタスクを定義
	auto lambda = [obj, mf, msg, srcfunc_copy, srcline, start, result, handle, this]() {
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto delay = std::chrono::steady_clock::now();
	    auto elapsed1 = std::chrono::duration_cast<std::chrono::milliseconds>(delay - start);	    
	    if (elapsed1.count() > FJDISPATCHLITE_PROFILE_TOO_DELAY_MSEC) {
		uint32_t timeMs;
		struct timespec ts;
		int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
		timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
		std::cerr << COLOR_RED << "[" << timeMs << "]:" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution is DELAYED. " << elapsed1.count() << " msec." << COLOR_RESET << std::endl;
	    }
#endif
	    int ret = (obj->*mf)(msg);
#if FJDISPATCHLITE_PROFILE_DBG == 1
	    auto now = std::chrono::steady_clock::now();
	    auto elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
	    if (elapsed2.count() > FJDISPATCHLITE_PROFILE_TOO_EXEC_MSEC) {
		uint32_t timeMs;
		struct timespec ts;
		int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
		timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
		std::cerr << COLOR_RED << "[" << timeMs << "]" << srcfunc_copy << "(" << srcline << "): *WARNING* function execution time is TOO LONG. " << elapsed2.count() << " msec." << COLOR_RESET << std::endl;
	    }
#endif

	    delete[] srcfunc_copy;

	    // 結果の登録
	    auto it = results_.find(handle);
            if (it != results_.end()) {
                it->second->value = ret;
                it->second->ready = true;
                it->second->cv.notify_all();
            }

        };

	auto task = std::make_unique<std::packaged_task<void()>>(lambda);

	// インスタンスのタスクキューに所有権を移動
        std::unique_lock<std::mutex> lock(mutex_);
	auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
        inst_info.task_queue.push(std::move(task));

	// インスタンスのタスクキューが実行中でないか、パラで動作させるフラグが立っていたら
	if (!inst_info.running) {
	    // 実行待ちタスクに登録して実行中に
	    ready_instances_.push(static_cast<FJUnitFrames*>(obj));
            inst_info.running = true;

	    // ワーカースレッドを必要に応じて拡張
	    if (ready_instances_.size() >= num_of_threads_ && num_of_threads_ < FJDISPATCHLITE_MAX_THREADS) {
		workers_.emplace_back(&FJDispatchLite::workerThread, this);
		++num_of_threads_;
#if FJDISPATCHLITE_DBG != 0
		std::cerr << COLOR_RED << "*WARNING* worker threads++ (" << num_of_threads_ << ")" << COLOR_RESET << std::endl;
#endif
	    }
            cv_.notify_one();
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
	fjt_handle_t handle;
	{
            std::lock_guard<std::mutex> lock(result_mutex_);
            handle = ++handle_counter_;
	}

#if FJDISPATCHLITE_DBG == 1
	{
	    uint32_t timeMs;
	    struct timespec ts;
	    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts); 
	    timeMs = ((uint32_t)ts.tv_sec * 1000) + ((uint32_t)ts.tv_nsec / 1000000);
	    std::cerr << COLOR_CYAN << "[" << timeMs << "]:" << srcfunc  << COLOR_RESET << std::endl;
	}
#endif

	// インスタンスのタスクキューに所有権を移動
        std::unique_lock<std::mutex> lock(mutex_);
	auto& inst_info = instance_map_[static_cast<FJUnitFrames*>(obj)];
	auto task_ptr = std::make_unique<std::packaged_task<void()>>(std::move(task));
	inst_info.task_queue.push(std::move(task_ptr));

	// インスタンスのタスクキューが実行中でないか、パラで動作させるフラグが立っていたら
	if (!inst_info.running) {
	    // 実行待ちタスクに登録して実行中に
	    ready_instances_.push(static_cast<FJUnitFrames*>(obj));
            inst_info.running = true;

	    // ワーカースレッドを必要に応じて拡張
	    if (ready_instances_.size() >= num_of_threads_ && num_of_threads_ < FJDISPATCHLITE_MAX_THREADS) {
		workers_.emplace_back(&FJDispatchLite::workerThread, this);
		++num_of_threads_;
#if FJDISPATCHLITE_DBG != 0
		std::cerr << COLOR_RED << "*WARNING* worker threads++ (" << num_of_threads_ << ")" << COLOR_RESET << std::endl;
#endif
	    }
            cv_.notify_one();
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
        auto start = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(result_mutex_);

        while (true) {
            auto it = results_.find(handle);
            if (it != results_.end()) {
                auto& item = it->second;
                if (item->ready) {
                    result_out = item->value;
                    return true;
                }
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
                if (elapsed.count() >= timeout_msec) return false;

                item->cv.wait_for(lock, std::chrono::milliseconds(timeout_msec) - elapsed);
            } else {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
                if (elapsed.count() >= timeout_msec) return false;
                result_cv_.wait_for(lock, std::chrono::milliseconds(50));
            }
        }
    }

private:
    /**
     * @brief 各FJUintFramesごとのインスタンス情報
     */
    struct InstanceInfo {
	std::queue<std::unique_ptr<std::packaged_task<void()>>> task_queue; //!< タスクキュー
        bool running = false; //!< このインスタンスのタスクがスレッドで実行中か
    };
    
    /**
     * @brief 各ハンドルごとの実行結果
     */
    struct ResultItem {
	int value; //!< タスクの返り値
	std::condition_variable cv; //!< タスク完了の状態変数
	bool ready = false; //!< 実行結果を受け取ったか
    };

    /**
     * @brief デフォルトコンストラクタ
     */
    FJDispatchLite() : stop_(false), num_of_threads_(FJDISPATCHLITE_DEFAULT_THREADS) {
        for(int i=0; i<num_of_threads_; ++i) {
            workers_.emplace_back(&FJDispatchLite::workerThread, this);
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
	std::lock_guard<std::mutex> lock(result_mutex_);
	return ++handle_counter_;
    }

    /**
     * @brief ワーカースレッドの実装
     */
    void workerThread() {
        while(true) {
            FJUnitFrames* inst = nullptr;
	    std::unique_ptr<std::packaged_task<void()>> task;

            {
                std::unique_lock<std::mutex> lock(mutex_);
		// 終了宣言済みか、または、実行待ちタスクがあるとき抜ける
                cv_.wait(lock, [this]{
                    return stop_ || !ready_instances_.empty();
                });

                if(stop_) return;

		// インスタンスをpop
                inst = ready_instances_.front();
                ready_instances_.pop();

                auto& inst_info = instance_map_[inst];
                if(inst_info.task_queue.empty()) {
		    // インスタンスのタスクキューが空ならば止める
                    inst_info.running = false;
                    continue;
                }

		// タスクの所有権をタスクキューからこのコンテキストに移動
                task = std::move(inst_info.task_queue.front());
                inst_info.task_queue.pop();
		
            }

            // タスク実行(排他範囲外にしておくこと)
            (*task)();

            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto& inst_info = instance_map_[inst];
                if(!inst_info.task_queue.empty()) {
		    // まだタスクキューが空でなかったら実行待ちタスクに登録
                    ready_instances_.push(inst);
                    cv_.notify_one();
                }
                else {
		    // このインスタンスで処理するものがなかったら止める
                    inst_info.running = false;
                }

            }
        }
    }

    std::mutex mutex_; //!< 排他
    std::condition_variable cv_; //!< 状態変数
    bool stop_; //!< 終了宣言変数
    std::vector<std::thread> workers_; //!< ワーカースレッド
    size_t num_of_threads_ = FJDISPATCHLITE_DEFAULT_THREADS; //!< ワーカースレッドの数

    std::unordered_map<FJUnitFrames*, InstanceInfo> instance_map_; //!< インスタンス管理テーブル
    std::queue<FJUnitFrames*> ready_instances_; //!< 実行待ちタスク

    std::mutex result_mutex_; //!< リザルト排他
    std::condition_variable result_cv_; //!< リザルト状態変数
    std::unordered_map<uint64_t, std::shared_ptr<ResultItem>> results_; //!< リザルトテーブル
    std::deque<uint64_t> result_order_;  //! 順序付きでリザルト保存
    uint64_t handle_counter_ = 0; //!< ハンドルカウンタ
};

#endif


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
 * @file fjtimerlite.h
 * @author FJD
 * @date 2025.5.29
 */
#ifndef __FJTIMERLITE_H__
#define __FJTIMERLITE_H__

#ifndef DOXYGEN_SKIP_THIS
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#endif

#include "fjtypes.h"

#define FJTIMERLITE_MIN_TICK_MSEC (15) //!< 最小ウェイト(msec)
#define FJTIMERLITE_MAX_TICK_MSEC (2000) //!< アイドル時ウェイト(msec)

#define FJTIMERLITE_PROFILE_DBG (1) //!< メソッド実行プロファイラ

// 前方参照
class FJDispatchLite;
class FJUnitFrames;

/**
 * @brief ワーカースレッド1つだけの簡易タイマー
 */
class FJTimerLite {
public:
    /**
     * @brief シングルトン
     */
    static FJTimerLite* GetInstance() {
        static FJTimerLite instance;
        return &instance;
    }

    /**
     * @brief デストラクタ
     */
    ~FJTimerLite() {
	{
	    std::unique_lock<std::mutex> lock(mutex_);
	    timers_.clear();
	    base_interval_msec_ = FJTIMERLITE_MAX_TICK_MSEC;
	    stop_ = true;
	}
	cond_var_.notify_one();
	if (worker_.joinable()) {
	    worker_.join();
	}
    }

    /**
     * @brief ベース周期設定
     * @note タイマー源はただ一つで、このウェイト未満の精度はありません。
     * @param[in] msec ベース周期(msec)
     * @retval [true] 設定成功
     * @retval [false] 範囲外
     */
    bool setBaseIntervalMs(uint16_t msec)
    {
        std::unique_lock<std::mutex> lock(mutex_);
	if (msec >= FJTIMERLITE_MIN_TICK_MSEC && msec <= FJTIMERLITE_MAX_TICK_MSEC) {
	    base_interval_msec_ = msec;
	    cond_var_.notify_one();
	    return true;
	}
	return false;
    }

    /*
     * @brief タイマー生成
     * @note 基本的にFJTIMERLITE_MIN_TICK_MSEC未満のタイマーは生成できない。また同一インスタンスを参照するFJTimerLiteとFJDispatchLiteのメソッド間は全く調停されないので、タイマーコールバックで _SendMsgSelf()するのが好適である。
     * @param[in] obj FJUnitFramesのポインタ
     * @param[in] mf FJUnitFramesのメソッド
     * @param[in] interval__msec 呼び出し周期(msec)
     * @param[in] srcfunc デバッグ表示用呼び出し関数名
     * @param[in] srcline デバッグ表示用呼び出し行数
     * @return ハンドル
     */
    template <typename T>
    fjt_handle_t createTimer(T* obj, int (T::*mf)(fjt_handle_t,fjt_time_t), uint32_t interval_msec, std::string srcfunc, uint32_t srcline) {
	static_assert(std::is_base_of<FJUnitFrames, T>::value, "T must derive from FJUnitFrames");

	if (interval_msec < FJTIMERLITE_MIN_TICK_MSEC) return 0;

        std::unique_lock<std::mutex> lock(mutex_);
	fjt_handle_t handle = FJDispatchLite::GetInstance()->getHandle();
	auto cb = std::bind(mf, obj, std::placeholders::_1, std::placeholders::_2);
	timers_.emplace(handle, TimerInfo(obj, cb, interval_msec, srcfunc, srcline));

	if (base_interval_msec_ > (interval_msec / 5)) {
	    base_interval_msec_ = interval_msec / 5;
	    cond_var_.notify_one();
	}

	return handle;
    }

    /*
     * @brief タイマー削除
     * @note コールバックメソッド内で実行しないこと。コールバック内でタイマーを止めたい場合は負値を返してください。
     * @param[in] handle ハンドル
     * @retval [true] 削除成功
     * @retval [false] 存在しないハンドル
     */
    bool removeTimer(fjt_handle_t handle) {
        std::unique_lock<std::mutex> lock(mutex_);
	if (timers_.find(handle) != timers_.end()) {
	    timers_[handle].active = false;
	    return true;
	}
	return false;
    }

    void removeTimer(void) {
        std::unique_lock<std::mutex> lock(mutex_);
	timers_.clear();
	base_interval_msec_ = FJTIMERLITE_MAX_TICK_MSEC;
    }

    /*
     * @brief タイマーがアクティブか
     * @param[in] handle ハンドル
     * @retval [true] アクテイブ
     * @retval [false] 存在しないハンドルか不活性
     */
    bool isActiveTimer(fjt_handle_t handle) {
        std::unique_lock<std::mutex> lock(mutex_);
	if (timers_.find(handle) != timers_.end()) {
	    return timers_[handle].active;
	}
	return false;
    }

private:
    /**
     * @brief タイマー情報
     */
    struct TimerInfo {
	FJUnitFrames *obj; //!< FJUnitFramesのポインタ
	std::function<int(fjt_handle_t,fjt_time_t)> mf; //!< FJUnitFramesの関数ポインタ
	uint32_t interval_msec; //!< 周期(msec)
	fjt_time_t next_time; //!< 次回実行時刻
	bool active; ///!< 活性状態
	fjt_time_t start; //!< 登録時刻
	std::string srcfunc; //!< 登録元関数名
	uint32_t srcline; //!< 登録元行数

	/**
	 * @brief デフォルトコンストラクタ
	 */
	TimerInfo(): obj(0), mf(0), interval_msec(0), active(0), srcfunc(""), srcline(0) {
	    start = next_time = std::chrono::steady_clock::now();
	};

	/**
	 * @brief コンストラクタ
	 * @param[in] o FJUnitFramesのポインタ
	 * @param[in] m FJUnitFramesの関数ポインタ
	 * @param[in] i 周期(msec)
	 */
	TimerInfo(FJUnitFrames *o, std::function<int(fjt_handle_t,fjt_time_t)> m, uint32_t i, std::string sf, uint32_t sl):
	    obj(o), mf(m), interval_msec(i), active(true), srcfunc(sf), srcline(sl) {
	    start = std::chrono::steady_clock::now(); next_time = start + std::chrono::milliseconds(interval_msec);
	};
    };

    /**
     * @brief コンストラクタ
     */
    FJTimerLite() : stop_(false), running_(false), base_interval_msec_(FJTIMERLITE_MAX_TICK_MSEC) {
        worker_ = std::thread(&FJTimerLite::TimerThread, this);
    }

    /**
     * @brief コピー禁止コンストラクタ
     */
    FJTimerLite(const FJTimerLite&) = delete;

    /**
     * @brief コピー禁止コンストラクタ
     */
    FJTimerLite& operator=(const FJTimerLite&) = delete;

    /**
     * @brief タイマーワーカースレッド
     */
    void TimerThread() {
	uint64_t wait;
	std::unordered_map<fjt_handle_t, TimerInfo>::iterator it;

	fjt_handle_t handle = 0;
	FJUnitFrames *obj = NULL;
	std::function<int(fjt_handle_t, fjt_time_t)> mf;
	int result = -1;
	fjt_time_t next_exec;
	
	{
	    std::unique_lock<std::mutex> lock(mutex_);
	    wait = base_interval_msec_;
	    it = timers_.begin();
	}

        while (true) {
	    {
		std::unique_lock<std::mutex> lock(mutex_);
		running_ = false;

		auto now = std::chrono::steady_clock::now();
		next_exec = now + std::chrono::milliseconds(FJTIMERLITE_MAX_TICK_MSEC);
		for (const auto& kv : timers_) {
		    const TimerInfo& t = kv.second;
		    if (t.active && t.next_time < next_exec)
			next_exec = t.next_time;
		}
		
		wait = std::chrono::duration_cast<std::chrono::milliseconds>(next_exec - now).count();
		if (wait < FJTIMERLITE_MIN_TICK_MSEC)
		    wait = FJTIMERLITE_MIN_TICK_MSEC;

		cond_var_.wait_for(lock, std::chrono::milliseconds(wait));

		if (stop_) return;

		if (it == timers_.end()) {
		    it = timers_.begin();
		    if (timers_.size() == 0) {
			base_interval_msec_ = FJTIMERLITE_MAX_TICK_MSEC;
		    }
		    wait = base_interval_msec_;
		    continue;
		}

		now = std::chrono::steady_clock::now();
		TimerInfo& timer = it->second;

		if (!timer.active || now < timer.next_time) {
		    ++it;
		    continue;
		}

		running_ = true;

#if FJTIMERLITE_PROFILE_DBG == 1
		{
		    auto delay = std::chrono::steady_clock::now();
		    auto elapsed1 = std::chrono::duration_cast<std::chrono::milliseconds>(delay - timer.start);	    
		    timer.start = delay;
		    std::cerr << timer.srcfunc << "(" << timer.srcline << "): *exec timer* delay = " << elapsed1.count() << " msec. " << std::endl;
		}
#endif
		handle = it->first;
		obj = timer.obj;
		mf = timer.mf;
		timer.next_time = now + std::chrono::milliseconds(timer.interval_msec);
	    }

	    auto task = [mf, obj, handle, &result]() {
		if (mf && obj) {
		    auto now = std::chrono::steady_clock::now();
		    result = mf(handle, now);
		}
	    };

	    task(); // 実行

	    {
		std::unique_lock<std::mutex> lock(mutex_);
		if (result < 0) {
		    if (timers_.find(handle) != timers_.end()) {
			timers_[handle].active = false;
		    }
		}
		wait = FJTIMERLITE_MIN_TICK_MSEC;
		++it;
	    }
	}
    }


    uint16_t base_interval_msec_; //!< ベース周期(msec)
    bool running_;
    bool stop_; //!< 終了宣言フラグ
    std::mutex mutex_; //!< 排他
    std::condition_variable cond_var_; //!< 状態変数
    std::thread worker_; //!< タイマーワーカースレッド

    std::unordered_map<fjt_handle_t, TimerInfo> timers_; //!< タイマー情報管理テーブル
};

#endif

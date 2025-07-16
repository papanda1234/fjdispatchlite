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
 * @file fjfixvector.h
 * @author FJD
 * @brief 事前確保されたメモリバッファ内で動作するstd::vectorもどき
 * @date 2025.7.7
 */
#ifndef __FJFIXVECTOR_H__
#define __FJFIXVECTOR_H__

#include <cstdint>
#include <cstring>
#include <functional>
#include <algorithm>

/**
 * @brief 固定長配列
 */
template<typename T>
class FJFixVector {
public:
    /**
     * @brief 固定長配列クラス
     * @param[in] buffer 確保済みバッファ
     * @param[in] buffer_size バッファサイズ
     * @param[in] count 予め確保済みバッファの場合要素数を継承
     */
    FJFixVector(char* buffer, size_t buffer_size, size_t &count)
        : buffer_(buffer),
          capacity_(buffer_size / sizeof(T)),
          count_ptr_(&count) {
	if (*count_ptr_ > capacity_) {
	    *count_ptr_ = capacity_;
	}
    }

    /**
     * @brief 要素の追加
     * @param[in] item 要素
     * @retval [true] 追加可能
     * @retval [false] バッファいっぱい
     */
    bool push_back(const T& item) {
        if (*count_ptr_ >= capacity_) return false;
        T* ptr = reinterpret_cast<T*>(buffer_ + *count_ptr_ * sizeof(T));
	std::memcpy(ptr, &item, sizeof(T));
        ++(*count_ptr_);
        return true;
    }

    /**
     * @brief 要素を先頭に追加
     * @param[in] item 要素
     * @retval [true] 追加成功
     * @retval [false] バッファいっぱい
     */
    bool push_front(const T& item) {
	if (*count_ptr_ >= capacity_) return false;
	T* base = reinterpret_cast<T*>(buffer_);
	std::memmove(base + 1, base, *count_ptr_ * sizeof(T));
	std::memcpy(base, &item, sizeof(T));
	++(*count_ptr_);
	return true;
    }

    /**
     * @brief 要素数
     * @return 要素数
     */
    size_t length() const {
        return *count_ptr_;
    }


    /**
     * @brief 要素数
     * @return 要素数
     */
    size_t size() const {
        return *count_ptr_;
    }

    /**
     * @brief 参照at
     * @return 必ずポインタ返し
     */
    T* at(size_t index) {
        if (index >= *count_ptr_) return nullptr;
        return reinterpret_cast<T*>(buffer_ + index * sizeof(T));
    }

    /**
     * @brief 参照[]
     * @return 必ずポインタ返し
     */
    T* operator[](size_t index) {
	if (index >= *count_ptr_) return nullptr;
	return reinterpret_cast<T*>(buffer_ + index * sizeof(T));
    }

    /**
     * @brief 要素の削除
     * @param[in] start 削除スタート番号
     * @param[in] count スタート番号以降何個消すか
     */
    void splice(size_t start, size_t count) {
        if (start >= *count_ptr_) return;
        if ((start + count) > *count_ptr_) count = *count_ptr_ - start;

        // shift memory
        size_t move_count = *count_ptr_ - (start + count);
        if (move_count > 0) {
            T* dst = at(start);
            T* src = at(start + count);
	    std::memmove(dst, src, move_count * sizeof(T));
        }

        *count_ptr_ = *count_ptr_ - count;
    }

    /**
     * @brief 要素のソート
     * @note list.sort([](const ListAtom& a, const ListAtom& b) { return a.msg_ > b.msg_; // 降順 });
     * @param[in] comp ラムダ式
     */
    void sort(std::function<bool(const T&, const T&)> comp) {
        // temporary pointer array for sorting
        T* base = reinterpret_cast<T*>(buffer_);
        std::sort(base, base + *count_ptr_, comp);
    }

private:
    char* buffer_; //!< バッファ先頭
    size_t capacity_;  //!< 最大要素数
    size_t* count_ptr_; //!< 要素数
};

#endif //__FJFIXVECTOR_H__

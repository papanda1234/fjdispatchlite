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
 * @file fjfixmap.h
 * @author FJD
 * @brief 事前確保されたメモリバッファ内で動作するstd::mapもどき
 * @date 2025.7.16
 */
#ifndef __FJFIXMAP_H__
#define __FJFIXMAP_H__

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>

template <typename T>
class FJFixMap {
 public:
    /**
     * @brief キー＆バリュー
     */
    struct Entry {
        uint32_t key; //!< 整数型固定キー
        T value; //!< C互換であること(スカラー値のみで構成されている構造体)
    };
    
    /**
     * @brief Constructor
     * @param[in] buf  事前確保されたメモリバッファ
     * @param[in] maxbufsize メモリバッファの最大バイト数
     * @param[in] count 要素数の参照
     */
    FJFixMap(char* buf, size_t maxbufsize, size_t &count)
	 : entries_(reinterpret_cast<Entry*>(buf))
	 , capacity_(0)
	 , count_ptr_(&count)
    {
	capacity_ = maxbufsize / sizeof(Entry);
	assert(*count_ptr_ <= capacity_);
    }

    /**
     * @brief 要素数
     * @return 要素数
     */
    size_t length() {
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
     * @brief 最大要素数
     * @return 最大要素数
     */
    size_t capacity() const {
        return capacity_;
    }

    /**
     * @brief 要素数クリア
     * @note メモリ領域には何のダメージも与えない
     */
    void clear() {
	*count_ptr_ = 0;
    }

    T* insert(uint32_t key) {
	T* p = find(key);
	if (p != nullptr) return p;

        assert(*count_ptr_ < capacity_);  // 領域超過チェック
	
        // 挿入位置を探す（キー昇順）
        size_t insert_pos = 0;
        while (insert_pos < *count_ptr_ && entries_[insert_pos].key < key)
            ++insert_pos;

        // 後ろをシフト
        for (size_t i = *count_ptr_; i > insert_pos; --i)
            entries_[i] = entries_[i - 1];

        entries_[insert_pos].key = key;
        ++(*count_ptr_);
        return &entries_[insert_pos].value;
    }

    /**
     * @brief 検索
     * @return 必ずポインタ返し
     */
    T* find(uint32_t key) {
        int idx = _find_index(key);
        return (idx >= 0) ? &entries_[idx].value : nullptr;
    }

    /**
     * @brief 削除
     * @param[in] key キー
     */
    bool unset(uint32_t key) {
	int idx = _find_index(key);
	if (idx < 0) return false;
	// 該当要素を詰めて削除
	for (size_t i = idx; i + 1 < *count_ptr_; ++i) {
	    entries_[i] = entries_[i + 1];
	}
	--(*count_ptr_);
	return true;
    }

 private:
    Entry* entries_; //*!< テーブル
    size_t capacity_; //!< 要素の最大数
    size_t* count_ptr_; //!< 要素数の参照

    /**
     * @brief 二分探索（キー昇順）
     * @param[in] key キー
     * @return 添字
     */
    int _find_index(uint32_t key) const {
        size_t left = 0, right = *count_ptr_;
        while (left < right) {
            size_t mid = (left + right) / 2;
            if (entries_[mid].key == key) return static_cast<int>(mid);
            if (entries_[mid].key < key) left = mid + 1;
            else right = mid;
        }
        return -1;
    }
};

#endif //__FJFIXMAP_H__

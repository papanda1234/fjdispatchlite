/**
 * @file fjfixvector.h
 * @author FJD
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
	 * @param[in] initial_size 予め確保済みバッファの場合要素数を継承
	 */
    FJFixVector(char* buffer, size_t buffer_size, size_t initial_size = 0)
        : buffer_(buffer),
          capacity_(buffer_size / sizeof(T)),
          length_(initial_size) {
		if (length_ > capacity_) {
			length_ = capacity_;
		}
	}

	/**
	 * @brief 要素の追加
	 * @param[in] item 要素
	 * @retval [true] 追加可能
	 * @retval [false] バッファいっぱい
	 */
    bool push_back(const T& item) {
        if (length_ >= capacity_) return false;
        T* ptr = reinterpret_cast<T*>(buffer_ + length_ * sizeof(T));
		std::memcpy(ptr, &item, sizeof(T));
        ++length_;
        return true;
    }

	/**
	 * @brief 要素を先頭に追加
	 * @param[in] item 要素
	 * @retval [true] 追加成功
	 * @retval [false] バッファいっぱい
	 */
	bool push_front(const T& item) {
		if (length_ >= capacity_) return false;
		T* base = reinterpret_cast<T*>(buffer_);
		std::memmove(base + 1, base, length_ * sizeof(T));
		std::memcpy(base, &item, sizeof(T));
		++length_;
		return true;
	}

	/**
	 * @brief 要素数
	 * @return 要素数
	 */
    size_t length() const {
        return length_;
    }

	/**
	 * @brief 参照at
	 * @return 必ずポインタ返し
	 */
    T* at(size_t index) {
        if (index >= length_) return nullptr;
        return reinterpret_cast<T*>(buffer_ + index * sizeof(T));
    }

	/**
	 * @brief 参照[]
	 * @return 必ずポインタ返し
	 */
	T* operator[](size_t index) {
		if (index >= length_) return nullptr;
		return reinterpret_cast<T*>(buffer_ + index * sizeof(T));
	}

	/**
	 * @brief 要素の削除
	 * @param[in] start 削除スタート番号
	 * @param[in] count スタート番号以降何個消すか
	 */
    void splice(size_t start, size_t count) {
        if (start >= length_) return;
        if (start + count > length_) count = length_ - start;

        // shift memory
        size_t move_count = length_ - (start + count);
        if (move_count > 0) {
            T* dst = at(start);
            T* src = at(start + count);
			std::memmove(dst, src, move_count * sizeof(T));
        }

        length_ -= count;
    }

	/**
	 * @brief 要素のソート
	 * @note list.sort([](const ListAtom& a, const ListAtom& b) { return a.msg_ > b.msg_; // 降順 });
	 * @param[in] comp ラムダ式
	 */
    void sort(std::function<bool(const T&, const T&)> comp) {
        // temporary pointer array for sorting
        T* base = reinterpret_cast<T*>(buffer_);
        std::sort(base, base + length_, comp);
    }

private:
    char* buffer_; //!< バッファ先頭
    size_t capacity_;  //!< 最大要素数
    size_t length_; //!< 要素数
};

#endif //__FJFIXVECTOR_H__

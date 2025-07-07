#include <iostream>
#include "fjfixvector.h"

struct ListAtom {
    int msg_;
    uint32_t time_;
};

int main() {
    char buf[1000];

    FJFixVector<ListAtom> list(buf, sizeof(buf));

    ListAtom obj1 = {11, 1234};
    list.push_back(obj1);

    ListAtom obj2 = {7, 2345};
    list.push_back(obj2);

    ListAtom obj3 = {15, 1111};
    list.push_back(obj3);

    std::cout << "Length: " << list.length() << std::endl;

	////// list2 /////
    FJFixVector<ListAtom> list2(buf, sizeof(buf), list.length()); // 同一領域を参照

    for (size_t i = 0; i < list2.length(); ++i) {
        ListAtom* p = list2.at(i);
        std::cout << "Item " << i << ": msg=" << p->msg_ << ", time=" << p->time_ << std::endl;
    }

    list2.sort([](const ListAtom& a, const ListAtom& b) {
        return a.msg_ > b.msg_; // 降順
    });
	////// list2ここまで /////

    std::cout << "--- After sort ---\n";
    for (size_t i = 0; i < list.length(); ++i) {
        ListAtom* p = list.at(i);
        std::cout << "Item " << i << ": msg=" << p->msg_ << ", time=" << p->time_ << std::endl;
    }

    list.splice(1, 1); // 1番目を削除

    std::cout << "--- After splice ---\n";
    for (size_t i = 0; i < list.length(); ++i) {
        ListAtom* p = list.at(i);
        std::cout << "Item " << i << ": msg=" << p->msg_ << ", time=" << p->time_ << std::endl;
    }

    return 0;
}

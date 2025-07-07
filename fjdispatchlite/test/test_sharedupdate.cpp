// test_sharedupdate.cpp
#include "fjsharedmem.h"
#include <iostream>
#include <unistd.h>

class Receiver : public FJSharedMem {
public:
    Receiver(const std::string& name) : FJSharedMem(name, 0) {
        addListen(this, 12345); // 受信するmsgを登録
		std::cerr << "listner" << this << std::endl;
    }

    void update(FJSharedMem*obj, fjt_msg_t msg) override {
        std::cout << "Received " << obj << " ,notification: msg = " << msg << "\n";
    }
};

int main() {
    Receiver receiver("/fjtestmem");

    std::cout << "Waiting for notification 20sec...\n";
	for (int i = 0; i < 20; i++) {
        sleep(1);
    }
    std::cout << "end...\n";

    return 0;
}

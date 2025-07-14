// test_sharedupdate.cpp
#include "fjsharedmem.h"
#include <iostream>
#include <unistd.h>

class Receiver : public FJSharedMem {
public:
    Receiver(const std::string& name, const char *srcfunc) : FJSharedMem(name, 0, srcfunc, std::vector<fjt_msg_t>{12345, 12346, 12347} ) {
    }

    void update(FJSharedMem*obj, fjt_msg_t msg) override {
        std::cout << "Received " << obj << " ,notification: msg = " << msg << "\n";

		int msg2 = 12345 + rand() % 2;
        std::cout << "Send " << this << " ,notify: msg = " << msg2 << "\n";
		notify(this, msg2);

    }
};

int main() {
    Receiver receiver("/fjtestmem", __PRETTY_FUNCTION__);

    std::cout << "Waiting for notification 15sec...\n";
	for (int i = 0; i < 15; i++) {
        sleep(15);
    }
    std::cout << "Done.\n";

    return 0;
}

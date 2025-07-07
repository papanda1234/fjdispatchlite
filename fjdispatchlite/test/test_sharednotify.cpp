// test_sharednotify.cpp
#include "fjsharedmem.h"
#include <iostream>
#include <unistd.h>

class Notifier : public FJSharedMem {
public:
    Notifier(const std::string& name) : FJSharedMem(name, 0) {}
    void update(FJSharedMem*, fjt_msg_t) override {}
};

int main() {
    Notifier notifier("/fjtestmem");
    std::cout << "Sending notification...\n";
    notifier.notify(&notifier, 12345); // 適当なmsg
    std::cout << "Done.\n";
	sleep(5);
    return 0;
}

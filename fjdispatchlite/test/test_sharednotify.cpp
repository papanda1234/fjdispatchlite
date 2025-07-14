// test_sharednotify.cpp
#include "fjsharedmem.h"
#include <iostream>
#include <unistd.h>

class Notifier : public FJSharedMem {
public:
    Notifier(const std::string& name, const char *srcfunc) : FJSharedMem(name, 0, srcfunc) {}
    void update(FJSharedMem*, fjt_msg_t) override {}
};

int main() {
    Notifier notifier("/fjtestmem", __PRETTY_FUNCTION__);
    std::cout << "Sending notification...\n";
    notifier.notify(&notifier, 12345); // 適当なmsg
    std::cout << "Done.\n";
	sleep(5);
    return 0;
}

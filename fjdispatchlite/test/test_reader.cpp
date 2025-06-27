// test/test_reader.cpp
#include "../fjmediaqueue.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <ctime>

int main() {
    try {
        FJMediaQueue queue("/fjmq_test", 1024, 8);
        char buf[1025];
        uint32_t size;
        uint64_t ts;

        while (true) {
            int ret = queue.timedwait(1000);
            if (ret == 0) {
                if (queue.read(buf, size, ts, 1000) == 0) {
                    buf[size] = '\0';
                    std::cout << "[ts=" << ts << "] " << buf << std::endl;
                }
            } else if (ret == ETIMEDOUT) {
                continue;
            } else {
                std::cerr << "timedwait error: " << strerror(ret) << std::endl;
                break;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

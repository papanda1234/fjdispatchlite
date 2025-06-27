// test/test_writer.cpp
#include "../fjmediaqueue.h"
#include <iostream>
#include <string>
#include <ctime>

int main() {
    try {
        FJMediaQueue queue("/fjmq_test", 1024, 8);
        std::string line;
        while (std::getline(std::cin, line)) {
            char buf[1025];
            size_t len = line.size();
            if (len >= 1024) len = 1023;
            memcpy(buf, line.c_str(), len);
            buf[len] = '\0';
            uint64_t ts = static_cast<uint64_t>(time(nullptr));
            int ret = queue.write(buf, len + 1, ts, 1000);
            if (ret != 0) {
                std::cerr << "write failed: " << ret << std::endl;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#include "SimpleShm.h"
#include <iostream>
#include <vector>
#include <chrono>

int main() {
    std::string shm_name = "/bigmsg_shm";
    std::cout << "[BIG SUB] Connecting..." << std::flush;
    ShmSubscriber sub(shm_name);
    std::cout << " OK. Waiting for big messages..." << std::endl;

    std::vector<uint8_t> data;
    std::string fname;
    size_t total_frames = 0;
    size_t total_bytes = 0;
    auto start = std::chrono::steady_clock::now();
    auto last_print = std::chrono::steady_clock::now();

    while (true) {
        if (sub.receive(data, &fname, 1000)) {
            total_frames++;
            total_bytes += data.size();

            // 每秒打印一次统计
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - last_print).count();
            if (sec >= 1.0) {
                double total_sec = std::chrono::duration<double>(now - start).count();
                std::cout << "[BIG SUB] Received: " << total_frames << " frames, "
                          << total_bytes / 1024 / 1024 << " MB, "
                          << total_frames / total_sec << " fps"
                          << std::endl;
                last_print = now;
            }
        }
    }
    return 0;
}

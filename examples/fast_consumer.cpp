// fast_consumer.cpp (多消费者版)
#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>

static std::atomic<bool> running{true};
void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    std::string shm_name = "/myshm";
    if (argc >= 2) shm_name = argv[1];

    SharedMemoryRingBuffer ring;
    while (running && !ring.Open(shm_name)) {
        std::cout << "Waiting for " << shm_name << "..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!running) return 0;

    std::cout << "Consumer started (MPMC). Ctrl+C to stop.\n";

    size_t total = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> buffer;
    std::string filename;

    while (running) {
        if (ring.Receive(buffer, filename, 100)) {   // 100ms 超时
            ++total;
            // 此处处理 buffer 和 filename ...
            if (total % 1000 == 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                std::cout << "Consumed " << total << " msgs, rate: " << total/elapsed << " msg/s\n";
            }
        }
    }

    auto end = std::chrono::steady_clock::now();
    double total_time = std::chrono::duration<double>(end - start).count();
    std::cout << "Total: " << total << ", avg: " << total/total_time << " msg/s\n";
    return 0;
}

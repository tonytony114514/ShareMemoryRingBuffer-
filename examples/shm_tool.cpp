#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <signal.h>
#include <iomanip>
#include <vector>
#include <string>

std::atomic<bool> running{true};
void signal_handler(int) { running = false; }

// ---- 生产者循环 ----
int run_sender(const std::string& shm_name, size_t msg_size, int interval_ms, bool create) {
    signal(SIGINT, signal_handler);
    SharedMemoryRingBuffer ring;
    if (create) {
        // 容量设为至少容纳 10000 条消息，但上限为 256MB
        size_t capacity = msg_size * 10000;
        if (capacity < 1024*1024) capacity = 1024*1024; // 最小1MB
        if (!ring.Create(shm_name, capacity)) {
            std::cerr << "Create failed\n";
            return 1;
        }
        std::cout << "Created shared memory: " << shm_name << " cap=" << capacity << "\n";
    } else {
        if (!ring.Open(shm_name)) {
            std::cerr << "Open failed\n";
            return 1;
        }
        std::cout << "Opened shared memory: " << shm_name << "\n";
    }

    std::vector<char> buffer(msg_size);
    for (size_t i = 0; i < msg_size; ++i) buffer[i] = static_cast<char>(i & 0xFF);

    size_t count = 0;
    size_t fail_count = 0;
    auto start = std::chrono::steady_clock::now();

    while (running) {
        if (ring.Send(buffer.data(), msg_size)) {
            ++count;
        } else {
            ++fail_count;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (interval_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        // 每1000条打印一次
        if ((count + fail_count) % 1000 == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed > 0) {
                std::cout << "Sent " << count << " msgs, fail " << fail_count
                          << ", rate: " << count/elapsed << " msg/s\n";
            }
        }
    }

    auto end = std::chrono::steady_clock::now();
    double total = std::chrono::duration<double>(end - start).count();
    std::cout << "Total sent: " << count << ", fails: " << fail_count
              << ", avg rate: " << (total>0 ? count/total : 0) << " msg/s\n";
    return 0;
}

// ---- 消费者循环 ----
int run_receiver(const std::string& shm_name, bool show_payload) {
    signal(SIGINT, signal_handler);
    SharedMemoryRingBuffer ring;
    if (!ring.Open(shm_name)) {
        std::cerr << "Open failed\n";
        return 1;
    }
    std::cout << "Receiver: opened " << shm_name << "\n";

    size_t count = 0;
    auto start = std::chrono::steady_clock::now();

    while (running) {
        Message* msg = ring.Receive();
        if (msg) {
            ++count;
            if (show_payload) {
                std::string data(reinterpret_cast<char*>(msg->payload), msg->header.length);
                std::cout << "Recv: " << data << "\n";
            }
            ring.Release(msg);
            if (count % 1000 == 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                if (elapsed > 0)
                    std::cout << "Received " << count << " msgs, rate: " << count/elapsed << " msg/s\n";
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    auto end = std::chrono::steady_clock::now();
    double total = std::chrono::duration<double>(end - start).count();
    std::cout << "Total received: " << count << ", avg rate: " << (total>0 ? count/total : 0) << " msg/s\n";
    return 0;
}

// ---- 监控 ----
int run_monitor(const std::string& shm_name) {
    signal(SIGINT, signal_handler);
    SharedMemoryRingBuffer ring;
    if (!ring.Open(shm_name)) {
        std::cerr << "Open failed\n";
        return 1;
    }

    std::cout << "Monitoring " << shm_name << " (Press Ctrl+C to stop)\n";
    while (running) {
        size_t cap = ring.Capacity();
        size_t readable = ring.AvailableRead();
        size_t writeable = ring.AvailableWrite();
        double usage = (double)readable / cap * 100.0;

        std::cout << std::fixed << std::setprecision(1)
                  << "Usage: " << usage << "%  |  Readable: " << readable
                  << "  Writeable: " << writeable
                  << "  Capacity: " << cap << "\r" << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "\nMonitor stopped.\n";
    return 0;
}

// ---- 主函数 ----
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " send <shm_name> [msg_size] [interval_ms] [create]\n"
                  << "  " << argv[0] << " receive <shm_name> [show]\n"
                  << "  " << argv[0] << " monitor <shm_name>\n"
                  << "Examples:\n"
                  << "  " << argv[0] << " send /myshm 64 10 create\n"
                  << "  " << argv[0] << " receive /myshm show\n"
                  << "  " << argv[0] << " monitor /myshm\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "send") {
        if (argc < 3) { std::cerr << "Missing shm_name\n"; return 1; }
        std::string shm_name = argv[2];
        size_t msg_size = 64;
        int interval_ms = 10;
        bool create = false;
        if (argc >= 4) msg_size = std::stoul(argv[3]);
        if (argc >= 5) interval_ms = std::stoi(argv[4]);
        if (argc >= 6 && std::string(argv[5]) == "create") create = true;
        return run_sender(shm_name, msg_size, interval_ms, create);
    } 
    else if (cmd == "receive" || cmd == "recv") {
        if (argc < 3) { std::cerr << "Missing shm_name\n"; return 1; }
        std::string shm_name = argv[2];
        bool show = false;
        if (argc >= 4 && std::string(argv[3]) == "show") show = true;
        return run_receiver(shm_name, show);
    }
    else if (cmd == "monitor" || cmd == "mon") {
        if (argc < 3) { std::cerr << "Missing shm_name\n"; return 1; }
        std::string shm_name = argv[2];
        return run_monitor(shm_name);
    }
    else {
        std::cerr << "Unknown command: " << cmd << "\n";
        return 1;
    }
}

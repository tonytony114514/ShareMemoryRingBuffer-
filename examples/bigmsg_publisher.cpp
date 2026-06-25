#include "SimpleShm.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>

static volatile sig_atomic_t running = 1;
void sigint_handler(int) { running = 0; }

int main() {
    signal(SIGINT, sigint_handler);
    std::string shm_name = "/bigmsg_shm";

    // 创建共享内存：容量 50 MB（足够缓冲数十张 1 MB 图片）
    std::cerr << "[BIG PUB] Creating shared memory..." << std::flush;
    ShmPublisher pub(shm_name, 50 * 1024 * 1024);
    std::cerr << " OK" << std::endl;

    // 生成 1 MB 模拟数据（内容不重要，全是 0xAB 即可）
    const size_t payload_size = 1024 * 1024; // 1 MB
    std::vector<uint8_t> dummy(payload_size, 0xAB);

    uint64_t fid = 0;
    auto start = std::chrono::steady_clock::now();

    while (running) {
        // 构造文件名
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string fname = "big_" + std::to_string(ts) + "_" + std::to_string(fid) + ".bin";

        // 发送（无限等待，不丢帧）
        if (!pub.publish(dummy.data(), payload_size, fname.c_str(), -1)) {
            std::cerr << "[BIG PUB] Unexpected send failure." << std::endl;
        }
        ++fid;

        // 控制发送速率：每秒 5 帧（5 MB/s），方便观察
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // 每秒打印一次统计
        auto now2 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now2 - start).count();
        if (elapsed >= 1.0) {
            std::cout << "[BIG PUB] Sent " << fid << " frames, "
                      << (fid * payload_size / 1024 / 1024) << " MB, "
                      << fid / elapsed << " fps" << std::endl;
            start = now2;
            fid = 0; // 重置计数器（仅统计显示用）
        }
    }

    std::cerr << "[BIG PUB] Finished." << std::endl;
    return 0;
}

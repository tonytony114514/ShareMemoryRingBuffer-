#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstring>
#include <sys/mman.h>    // shm_unlink

static volatile sig_atomic_t running = 1;
void sigint_handler(int) { running = 0; }

struct Point3D {
    float x, y, z;
    float intensity;
    uint64_t timestamp;
    uint8_t padding[40];
};
static_assert(sizeof(Point3D) == 64, "Point3D must be 64 bytes");

int main() {
    signal(SIGINT, sigint_handler);

    const char* shm_name = "/myshm";
    size_t points_per_frame = 500;
    size_t payload_size = points_per_frame * sizeof(Point3D);
    
    // 计算一条消息的大致总长度（64字节对齐）
    std::string demo_name = "frame_xxxxx.bin";   // 典型文件名长度
    size_t one_msg_total = ((sizeof(MessageHeader) + demo_name.size() + payload_size) + 63) / 64 * 64;
    // 容量至少容纳 20 条消息，且必须是 2 的幂
    size_t shm_size = 1;
    while (shm_size < one_msg_total * 20) shm_size <<= 1;

    SharedMemoryRingBuffer ring;
    bool opened = ring.Open(shm_name);
    
    if (opened) {
        std::cout << "Opened existing shared memory: " << shm_name << std::endl;
    } else {
        // 尝试创建前，先清理可能残留的同名共享内存（避免创建失败）
        shm_unlink(shm_name);
        if (!ring.Create(shm_name, shm_size)) {
            std::cerr << "Create failed (size=" << shm_size << ")" << std::endl;
            return 1;
        }
        std::cout << "Created shared memory, size=" << shm_size << std::endl;
    }

    // 准备点云数据
    std::vector<Point3D> frame(points_per_frame);
    int frame_count = 0;

    std::cout << "Starting continuous send... (Ctrl+C to stop)" << std::endl;
    while (running) {
        // 生成模拟点云
        for (size_t i = 0; i < points_per_frame; ++i) {
            frame[i].x = static_cast<float>(frame_count) + i * 0.1f;
            frame[i].y = static_cast<float>(frame_count) + i * 0.2f;
            frame[i].z = static_cast<float>(frame_count) + i * 0.3f;
            frame[i].intensity = 0.5f;
            frame[i].timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::string filename = "frame_" + std::to_string(frame_count) + ".bin";
        if (!ring.TrySend(frame.data(), payload_size, 200, filename.c_str())) {
            std::cerr << "Send failed (frame " << frame_count << ")" << std::endl;
        } else {
            std::cout << "Sent frame " << frame_count
                      << " (" << points_per_frame << " points)" << std::endl;
        }
        ++frame_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // 10 fps
    }

    std::cout << "Exiting, total frames sent: " << frame_count << std::endl;
    return 0;
}

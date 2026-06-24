#include "SimpleShm.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>

struct Point3D {
    float x, y, z;
    float intensity;
    uint64_t timestamp;
    uint8_t padding[40]; // 对齐 64 字节
};
static_assert(sizeof(Point3D) == 64, "Point3D must be 64 bytes");

static volatile sig_atomic_t running = 1;
void sigint_handler(int) { running = 0; }

int main() {
    signal(SIGINT, sigint_handler);
    std::string shm_name = "/demo_shm";          // 与摄像头共用同一块共享内存

    std::cerr << "[LIDAR] Opening shared memory..." << std::flush;
    ShmPublisher pub(shm_name, 20 * 1024 * 1024); // 20 MB
    std::cerr << " OK" << std::endl;

    const int points_per_frame = 500;
    std::vector<Point3D> points(points_per_frame);
    uint64_t fid = 0;

    while (running) {
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (int i = 0; i < points_per_frame; ++i) {
            points[i].x = (fid + i) * 0.1f;
            points[i].y = (fid + i) * 0.2f;
            points[i].z = (fid + i) * 0.3f;
            points[i].intensity = 0.5f;
            points[i].timestamp = now_us + i;
        }

        std::string fname = "lidar_" + std::to_string(fid) + ".bin";

        // 关键：timeout_ms = -1，绝不舍弃帧
        if (!pub.publish(points.data(), points.size() * sizeof(Point3D), fname.c_str(), -1)) {
            std::cerr << "[LIDAR] Unexpected send failure." << std::endl;
        }
        ++fid;
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 fps
    }
    std::cerr << "[LIDAR] Sent " << fid << " frames." << std::endl;
    return 0;
}

#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <cstring>
#include <signal.h>
#include <iomanip>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
struct Point3D {
    float x, y, z;
    float intensity;
    uint64_t timestamp;
    uint8_t padding[40];
};
static_assert(sizeof(Point3D) == 64, "Point3D must be 64 bytes");

static std::atomic<bool> running{true};
void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    std::string shm_name = "/myshm";
    int points_per_frame = 1000;
    int interval_ms = 10;
    int frame_count = 0;

    if (argc >= 2) shm_name = argv[1];
    if (argc >= 3) points_per_frame = std::stoi(argv[2]);
    if (argc >= 4) interval_ms = std::stoi(argv[3]);
    if (argc >= 5) frame_count = std::stoi(argv[4]);

    size_t data_size = points_per_frame * sizeof(Point3D);
    size_t shm_cap = 1;
    while (shm_cap < data_size * 2) shm_cap <<= 1;

    // ====== 强制清理旧对象（忽略错误） ======
    shm_unlink(shm_name.c_str());

    SharedMemoryRingBuffer ring;
    if (!ring.Create(shm_name, shm_cap)) {
        std::cerr << "Failed to create shared memory " << shm_name << std::endl;
        return 1;
    }

    std::cout << "LiDAR simulator started. SHM: " << shm_name
              << ", points/frame: " << points_per_frame
              << ", interval: " << interval_ms << "ms"
              << (frame_count>0 ? ", frames: " + std::to_string(frame_count) : ", infinite")
              << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> coord(-100.0f, 100.0f);
    std::uniform_real_distribution<float> intensity(0.0f, 1.0f);

    std::vector<Point3D> points(points_per_frame);
    int sent_frames = 0;
    auto start_time = std::chrono::steady_clock::now();
    int retry_count = 0;

    while (running && (frame_count == 0 || sent_frames < frame_count)) {
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (int i = 0; i < points_per_frame; ++i) {
            points[i].x = coord(gen);
            points[i].y = coord(gen);
            points[i].z = coord(gen);
            points[i].intensity = intensity(gen);
            points[i].timestamp = now_us + i;
        }

        std::string filename = "lidar_frame_" + std::to_string(sent_frames) + ".bin";

        while (running && !ring.TrySend(points.data(), data_size, 10, filename.c_str())) {
            ++retry_count;
            if (retry_count % 1000 == 0) {
                std::cout << "Waiting for buffer space... (retries: " << retry_count << ")\n";
            }
        }
        if (!running) break;

        ++sent_frames;
        if (sent_frames % 100 == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            std::cout << "Sent " << sent_frames << " frames, rate: " << sent_frames/elapsed << " fps" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    std::cout << "Sent total " << sent_frames << " frames. Exiting." << std::endl;
    return 0;
}

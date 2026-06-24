#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>          // 新增
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

// 示例结构体：3D点云（对齐到64字节，避免伪共享）
struct Point3D {
    float x, y, z;
    float intensity;
    uint64_t timestamp;
    uint8_t padding[40];   // 修正：24字节 + 40填充 = 64
};
static_assert(sizeof(Point3D) == 64, "Point3D must be 64 bytes");

int main(int argc, char* argv[]) {
    int num_points = 1000;
    if (argc >= 2) {
        num_points = std::stoi(argv[1]);
        if (num_points <= 0) num_points = 1000;
    }

    size_t data_size = num_points * sizeof(Point3D);
    size_t shm_size = 1 << 20;
    while (shm_size < data_size * 2) {
        shm_size <<= 1;
    }
    std::cout << "Generating " << num_points << " points (" << data_size << " bytes)" << std::endl;
    std::cout << "Shared memory size: " << shm_size << " bytes" << std::endl;

    const std::string shm_name = "struct_test_shm";
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // 子进程：消费者
        SharedMemoryRingBuffer ring;
        if (!ring.Open(shm_name)) {
            std::cerr << "Child: Open failed\n";
            return 1;
        }
        std::cout << "Child: Opened shared memory, waiting for point cloud...\n";

        Message* msg = nullptr;
        while (true) {
            msg = ring.Receive();
            if (msg) break;
            usleep(10000);
        }

        size_t received_points = msg->header.length / sizeof(Point3D);
        const Point3D* points = reinterpret_cast<const Point3D*>(msg->payload);

        std::cout << "Child: Received " << received_points << " points." << std::endl;
        int show = std::min(5, (int)received_points);
        for (int i = 0; i < show; ++i) {
            std::cout << "  Point[" << i << "]: (" 
                      << points[i].x << ", " << points[i].y << ", " << points[i].z 
                      << ") intensity=" << points[i].intensity 
                      << " ts=" << points[i].timestamp << std::endl;
        }

        ring.Release(msg);
        std::cout << "Child: done." << std::endl;
        return 0;

    } else {
        // 父进程：生产者
        SharedMemoryRingBuffer ring;
        if (!ring.Create(shm_name, shm_size)) {
            std::cerr << "Parent: Create failed\n";
            return 1;
        }
        std::cout << "Parent: Created shared memory, generating point cloud...\n";

        std::vector<Point3D> points(num_points);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> coord_dist(-100.0f, 100.0f);
        std::uniform_real_distribution<float> intensity_dist(0.0f, 1.0f);
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        for (int i = 0; i < num_points; ++i) {
            points[i].x = coord_dist(gen);
            points[i].y = coord_dist(gen);
            points[i].z = coord_dist(gen);
            points[i].intensity = intensity_dist(gen);
            points[i].timestamp = now + i;
        }

        if (!ring.Send(points.data(), data_size)) {
            std::cerr << "Parent: Send failed\n";
            ring.Destroy();
            return 1;
        }
        std::cout << "Parent: Point cloud sent (" << data_size << " bytes)\n";

        int status;
        waitpid(pid, &status, 0);

        ring.Destroy();
        std::cout << "Parent: done." << std::endl;
        return 0;
    }
}

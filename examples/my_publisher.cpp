#include "SimpleShm.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

static volatile sig_atomic_t running = 1;
void sigint_handler(int) { running = 0; }

int main() {
    signal(SIGINT, sigint_handler);
    try {
        ShmPublisher pub("/myshm", 1024*1024);
        int frame_id = 0;
        while (running) {
            std::vector<float> points(3000); // 模拟一帧数据
            // 填充数据（实际中换成真实数据）
            for (auto& v : points) v = static_cast<float>(frame_id) * 0.01f;

            std::string fname = "frame_" + std::to_string(frame_id) + ".bin";
            if (!pub.publish(points.data(), points.size() * sizeof(float), fname.c_str(), 200)) {
                std::cerr << "Send timeout at frame " << frame_id << std::endl;
            } else {
                std::cout << "Sent frame " << frame_id << " (" << points.size() << " points)" << std::endl;
            }
            ++frame_id;
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10 fps
        }
    } catch (const std::exception& e) {
        std::cerr << "Publisher error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

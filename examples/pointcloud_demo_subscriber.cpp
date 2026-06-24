#include "SimpleShm.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <exception>

int main() {
    std::string shm_name = "/demo_shm";
    std::cout << "[PCD SUB] Connecting..." << std::flush;
    ShmSubscriber sub(shm_name);
    std::cout << " OK. Waiting for point cloud frames..." << std::endl;

    std::vector<uint8_t> data;
    std::string fname;
    int lidar_frames = 0, camera_frames = 0;
    size_t lidar_bytes = 0, camera_bytes = 0;
    auto start = std::chrono::steady_clock::now();
    auto last_print = std::chrono::steady_clock::now();

    while (true) {
        try {
            if (sub.receive(data, &fname, 1000)) {
                // 按文件名前缀分类统计
                if (fname.find("lidar_") == 0) {
                    ++lidar_frames;
                    lidar_bytes += data.size();
                } else if (fname.find("cam_") == 0) {
                    ++camera_frames;
                    camera_bytes += data.size();
                }
                // 忽略其他未知文件

                // 每秒打印统计（修改为 1 秒，刷新更快）
                auto now = std::chrono::steady_clock::now();
                double sec = std::chrono::duration<double>(now - last_print).count();
                if (sec >= 5.0) {
                    double total_sec = std::chrono::duration<double>(now - start).count();
                    std::cout << "[PCD SUB] --- Stats ---\n"
                              << "  Lidar: " << lidar_frames << " frames, "
                              << lidar_bytes / 1024 / 1024.0 << " MB, "
                              << (total_sec > 0 ? lidar_frames / total_sec : 0) << " fps\n"
                              << "  Camera: " << camera_frames << " frames, "
                              << camera_bytes / 1024 / 1024.0 << " MB, "
                              << (total_sec > 0 ? camera_frames / total_sec : 0) << " fps"
                              << std::endl;
                    last_print = now;
                }
            }
            // 1 秒超时没收到消息，可以打印提示（可选）
            // else { std::cout << "[PCD SUB] No message in 1s." << std::endl; }
        }
        catch (const std::exception& e) {
            std::cerr << "[PCD SUB] Exception: " << e.what() << ". Continuing..." << std::endl;
            // 可以选择重置缓冲区
            // sub.resetBuffer();
        }
        catch (...) {
            std::cerr << "[PCD SUB] Unknown exception. Continuing..." << std::endl;
        }
    }
    return 0;
}

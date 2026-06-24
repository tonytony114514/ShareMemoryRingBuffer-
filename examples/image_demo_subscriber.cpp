#include "SimpleShm.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>

int main() {
    std::string shm_name = "/demo_shm";
    std::cout << "[IMG SUB] Connecting..." << std::flush;
    ShmSubscriber sub(shm_name);
    std::cout << " OK. Waiting for camera frames..." << std::endl;

    std::vector<uint8_t> data;
    std::string fname;
    cv::namedWindow("Demo Image Stream", cv::WINDOW_NORMAL);

    int frames = 0, ignored = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        if (sub.receive(data, &fname, 1000)) {
            // 只处理摄像头帧（文件名以 "cam_" 开头）
            if (fname.find("cam_") == 0) {
                cv::Mat frame = cv::imdecode(data, cv::IMREAD_COLOR);
                if (!frame.empty()) {
                    cv::imshow("Demo Image Stream", frame);
                    ++frames;
                    if (frames % 30 == 0) {
                        auto now = std::chrono::steady_clock::now();
                        double sec = std::chrono::duration<double>(now - start).count();
                        std::cout << "[IMG SUB] Received " << frames << " images, "
                                  << frames / sec << " fps" << std::endl;
                    }
                } else {
                    std::cout << "[IMG SUB] Decode error for " << fname << std::endl;
                }
            } else {
                ++ignored;
                // 可选：每忽略很多条打印一次
                if (ignored % 100 == 0)
                    std::cout << "[IMG SUB] Ignored " << ignored << " non-camera messages." << std::endl;
            }
            char key = (char)cv::waitKey(1);
            if (key == 27 || key == 'q') break;
        }
    }
    cv::destroyAllWindows();
    return 0;
}

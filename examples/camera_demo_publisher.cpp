#include "SimpleShm.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

static volatile sig_atomic_t running = 1;
void sigint_handler(int) { running = 0; }

cv::Mat generateDummyFrame(int w, int h, int fid) {
    cv::Mat img(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.at<cv::Vec3b>(y,x)[0] = (x + fid) % 256;
            img.at<cv::Vec3b>(y,x)[1] = (y + fid) % 256;
            img.at<cv::Vec3b>(y,x)[2] = ((x+y)/2 + fid) % 256;
        }
    }
    return img;
}

int main() {
    signal(SIGINT, sigint_handler);
    std::string shm_name = "/demo_shm";

    // 1. 先创建共享内存（20 MB，足够缓冲数百帧）
    std::cerr << "[CAM] Creating shared memory..." << std::flush;
    ShmPublisher pub(shm_name, 20 * 1024 * 1024);
    std::cerr << " OK" << std::endl;

    cv::Mat frame;
    std::vector<uchar> jpeg_buf;
    uint64_t fid = 0;

    while (running) {
        // 2. 生成模拟帧（640x480，彩色条纹动态变化）
        frame = generateDummyFrame(640, 480, fid);

        // 3. JPEG 压缩（质量 80%，大小约 20~50 KB）
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
        cv::imencode(".jpg", frame, jpeg_buf, params);

        // 4. 构造文件名（时间戳 + 帧号）
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string fname = "cam_" + std::to_string(ts) + "_" + std::to_string(fid) + ".jpg";

        // 5. 发送（timeout_ms = -1：无限等待，绝不舍弃帧）
        if (!pub.publish(jpeg_buf.data(), jpeg_buf.size(), fname.c_str(), -1)) {
            // 正常情况下不会进入这里，除非共享内存被意外销毁
            std::cerr << "[CAM] Unexpected send failure." << std::endl;
        }
        ++fid;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
    }
    std::cerr << "[CAM] Sent " << fid << " frames." << std::endl;
    return 0;
}

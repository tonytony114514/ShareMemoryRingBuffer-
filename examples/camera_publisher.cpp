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

int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);
    std::string shm_name = "/camera_shm";
    if (argc >= 2) shm_name = argv[1];

    int width = 640, height = 480;
    std::cerr << "Creating shared memory..." << std::flush;
    ShmPublisher pub(shm_name, 10 * 1024 * 1024);
    std::cerr << " OK (using simulated images)" << std::endl;

    cv::Mat frame;
    std::vector<uchar> jpeg_buf;
    uint64_t frame_id = 0;

    while (running) {
        frame = generateDummyFrame(width, height, frame_id);
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
        cv::imencode(".jpg", frame, jpeg_buf, params);

        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string fname = std::to_string(ts) + "_" + std::to_string(frame_id) + ".jpg";

        if (!pub.publish(jpeg_buf.data(), jpeg_buf.size(), fname.c_str(), 10)) {
            std::cerr << "Dropped frame " << frame_id << std::endl;
        }
        ++frame_id;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    std::cerr << "Sent " << frame_id << " frames." << std::endl;
    return 0;
}

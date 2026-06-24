#include "SimpleShm.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    std::string shm_name = "/camera_shm";
    if (argc >= 2) shm_name = argv[1];

    try {
        ShmSubscriber sub(shm_name);
        std::cout << "[ OK ] Subscriber connected. Waiting for frames..." << std::endl;

        std::vector<uint8_t> data;
        std::string fname;
        int received = 0;
        int shown = 0;

        cv::namedWindow("Received Frame", cv::WINDOW_NORMAL);

        while (true) {
            if (sub.receive(data, &fname, 1000)) {   // 1秒超时
                received++;
                std::cout << "[RECV] #" << received
                          << " | size: " << data.size() << " bytes"
                          << " | file: " << fname << std::flush;

                cv::Mat frame = cv::imdecode(data, cv::IMREAD_COLOR);
                if (!frame.empty()) {
                    cv::imshow("Received Frame", frame);
                    std::cout << " -> DISPLAYED" << std::endl;
                    shown++;
                } else {
                    std::cout << " -> DECODE ERROR" << std::endl;
                }

                char key = (char)cv::waitKey(1);
                if (key == 27 || key == 'q') break;
            } else {
                std::cout << "[WAIT] No frame received in 1s..." << std::endl;
            }
        }

        std::cout << "Exiting. Total received: " << received
                  << ", displayed: " << shown << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    cv::destroyAllWindows();
    return 0;
}

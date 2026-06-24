#include "SimpleShm.h"
#include <iostream>
#include <iomanip>

int main() {
    try {
        ShmSubscriber sub("/myshm");

        // 启动监控线程，每秒打印队列深度和吞吐量
        sub.startMonitor([](size_t qsize, double rate) {
            std::cout << "[Monitor] Queue: " << qsize
                      << " msgs, Throughput: " << std::fixed << std::setprecision(1)
                      << rate << " msg/s" << std::endl;
        });

        std::vector<uint8_t> data;
        std::string filename;
        while (true) {
            if (sub.receive(data, &filename, 500)) {
                std::cout << "Received " << data.size() << " bytes, file="
                          << (filename.empty() ? "none" : filename) << std::endl;
                // 处理数据...
            }
            // 此处可添加退出条件
        }
    } catch (const std::exception& e) {
        std::cerr << "Subscriber error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

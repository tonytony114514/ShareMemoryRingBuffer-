#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <create|open> <shm_name> [size]\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string name = argv[2];
    SharedMemoryRingBuffer ring;

    if (mode == "create") {
        size_t size = (argc > 3) ? std::stoul(argv[3]) : 2 * 1024 * 1024;
        if (!ring.Create(name, size)) {
            std::cerr << "Create failed\n";
            return 1;
        }
        std::cout << "Created shared memory " << name << " with size " << size << "\n";

        for (int i = 0; i < 10; ++i) {
            std::string msg = "Message " + std::to_string(i);
            if (!ring.Send(msg.c_str(), msg.size() + 1)) {
                std::cerr << "Send failed\n";
                break;
            }
            std::cout << "Sent: " << msg << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Press Enter to destroy shared memory and exit...\n";
        std::cin.get();
        ring.Destroy();
    } else if (mode == "open") {
        if (!ring.Open(name)) {
            std::cerr << "Open failed\n";
            return 1;
        }
        std::cout << "Opened shared memory " << name << "\n";

        while (true) {
            Message* msg = ring.Receive();
            if (msg) {
                std::string data(reinterpret_cast<char*>(msg->payload), msg->header.length);
                std::cout << "Received: " << data << "\n";
                ring.Release(msg);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    } else {
        std::cerr << "Invalid mode\n";
        return 1;
    }
    return 0;
}

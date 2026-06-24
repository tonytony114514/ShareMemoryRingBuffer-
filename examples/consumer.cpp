#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <shm_name>\n";
        return 1;
    }
    std::string name = argv[1];

    SharedMemoryRingBuffer ring;
    if (!ring.Open(name)) {
        std::cerr << "Failed to open shared memory\n";
        return 1;
    }

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
    return 0;
}

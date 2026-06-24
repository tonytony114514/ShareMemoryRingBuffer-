#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <thread>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <shm_name> <num_messages>\n";
        return 1;
    }
    std::string name = argv[1];
    int count = std::stoi(argv[2]);

    SharedMemoryRingBuffer ring;
    if (!ring.Open(name)) {
        std::cerr << "Failed to open shared memory\n";
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        std::string msg = "Producer message " + std::to_string(i);
        if (!ring.Send(msg.c_str(), msg.size() + 1)) {
            std::cerr << "Send failed\n";
            break;
        }
        std::cout << "Sent: " << msg << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
}

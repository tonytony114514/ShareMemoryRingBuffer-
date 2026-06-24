#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

int main(int argc, char* argv[]) {
    // 默认图片文件名
    std::string input_file = "test.jpg";
    std::string output_file = "output.jpg";

    if (argc >= 2) {
        input_file = argv[1];
    }
    if (argc >= 3) {
        output_file = argv[2];
    }

    // 检查源文件是否存在
    std::ifstream ifs(input_file, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "Error: cannot open input file: " << input_file << std::endl;
        std::cerr << "Usage: " << argv[0] << " [input_image] [output_image]" << std::endl;
        std::cerr << "Example: " << argv[0] << " photo.jpg received.jpg" << std::endl;
        return 1;
    }

    size_t file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<char> file_data(file_size);
    ifs.read(file_data.data(), file_size);
    ifs.close();

    std::cout << "Loaded image: " << input_file << " (" << file_size << " bytes)" << std::endl;

    // 确保缓冲区足够大
    size_t shm_size = 1 << 22; 
    while (shm_size < file_size * 2) {
        shm_size <<= 1; 
    }
    std::cout << "Shared memory size: " << shm_size << " bytes" << std::endl;

    const std::string shm_name = "image_test_shm";
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // ---------- 子进程：消费者 ----------
        SharedMemoryRingBuffer ring;
        if (!ring.Open(shm_name)) {
            std::cerr << "Child: Open failed\n";
            return 1;
        }
        std::cout << "Child: Opened shared memory, waiting for image...\n";

        // 等待接收一条消息（图片数据）
        Message* msg = nullptr;
        while (true) {
            msg = ring.Receive();
            if (msg) break;
            usleep(10000);
        }

        // 保存收到的数据
        std::ofstream ofs(output_file, std::ios::binary);
        if (!ofs) {
            std::cerr << "Child: cannot create output file: " << output_file << std::endl;
            ring.Release(msg);
            return 1;
        }
        ofs.write(reinterpret_cast<const char*>(msg->payload), msg->header.length);
        ofs.close();
        std::cout << "Child: saved image to " << output_file
                  << " (" << msg->header.length << " bytes)" << std::endl;

        ring.Release(msg);
        std::cout << "Child: done." << std::endl;
        return 0;

    } else {
        // ---------- 父进程：生产者 ----------
        SharedMemoryRingBuffer ring;
        if (!ring.Create(shm_name, shm_size)) {
            std::cerr << "Parent: Create failed\n";
            return 1;
        }
        std::cout << "Parent: Created shared memory, sending image...\n";

        // 发送图片二进制数据
        if (!ring.Send(file_data.data(), file_size)) {
            std::cerr << "Parent: Send failed (image too large?)\n";
            ring.Destroy();
            return 1;
        }
        std::cout << "Parent: Image sent (" << file_size << " bytes)\n";

        // 等待子进程结束
        int status;
        waitpid(pid, &status, 0);

        // 清理共享内存
        ring.Destroy();
        std::cout << "Parent: done." << std::endl;
        return 0;
    }
}

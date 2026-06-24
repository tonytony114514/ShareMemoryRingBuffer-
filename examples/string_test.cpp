#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

int main() {
    const std::string shm_name = "string_test_shm";
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
        std::cout << "Child: Opened shared memory, waiting for messages...\n";

        while (true) {
            Message* msg = ring.Receive();
            if (msg) {
                std::string data(reinterpret_cast<char*>(msg->payload), msg->header.length);
                std::cout << "Child received: " << data << "\n";
                ring.Release(msg);
                if (data == "END") break;   // 收到结束信号退出
            } else {
                usleep(10000);  // 无数据时稍等
            }
        }
        std::cout << "Child exiting.\n";
        return 0;

    } else {
        // ---------- 父进程：生产者 ----------
        SharedMemoryRingBuffer ring;
        if (!ring.Create(shm_name, 1 << 20)) {  // 1 MB 容量
            std::cerr << "Parent: Create failed\n";
            return 1;
        }
        std::cout << "Parent: Created shared memory, sending messages...\n";

        const char* msgs[] = {"Hello", "World", "C++", "Shared Memory", "END"};
        for (auto msg : msgs) {
            if (!ring.Send(msg, strlen(msg) + 1)) {
                std::cerr << "Parent: Send failed for " << msg << "\n";
            } else {
                std::cout << "Parent sent: " << msg << "\n";
            }
            usleep(100000);  // 间隔 0.1 秒，让子进程有时间处理
        }

        // 等待子进程退出
        int status;
        waitpid(pid, &status, 0);

        // 清理共享内存
        ring.Destroy();
        std::cout << "Parent: done.\n";
        return 0;
    }
}

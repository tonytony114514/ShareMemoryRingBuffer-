#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>

// 生成测试数据：每个字节为 (偏移量 % 256)
static void fill_pattern(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
}

// 验证数据是否匹配模式
static bool verify_pattern(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != static_cast<uint8_t>(i & 0xFF)) {
            return false;
        }
    }
    return true;
}

int main() {
    // 定义测试的大小序列（字节）
    std::vector<size_t> test_sizes = {
        1,          // 最小
        16,
        64,
        128,
        256,
        512,
        1024,       // 1 KB
        4096,       // 4 KB
        16384,      // 16 KB
        65536,      // 64 KB
        262144,     // 256 KB
        524288,     // 512 KB
        1048576     // 1 MB (最大，需 ≤ 容量/2)
    };

    // 共享内存容量设置为 2 MB（足够容纳 1 MB 消息）
    const size_t SHM_CAPACITY = 2 * 1024 * 1024;
    const std::string shm_name = "any_size_test_shm";

    // 创建管道用于父子同步
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // ---------- 子进程：消费者 ----------
        close(pipe_fd[1]); // 关闭写端
        SharedMemoryRingBuffer ring;
        if (!ring.Open(shm_name)) {
            std::cerr << "Child: Open failed\n";
            close(pipe_fd[0]);
            return 1;
        }
        std::cout << "Child: Opened shared memory, ready to receive.\n";

        for (size_t expected_len : test_sizes) {
            // 接收一条消息
            Message* msg = nullptr;
            while (true) {
                msg = ring.Receive();
                if (msg) break;
                usleep(1000); // 1ms
            }

            // 验证长度
            if (msg->header.length != expected_len) {
                std::cerr << "Child: Length mismatch! Expected " << expected_len
                          << ", got " << msg->header.length << std::endl;
                ring.Release(msg);
                close(pipe_fd[0]);
                return 1;
            }

            // 验证数据模式
            if (!verify_pattern(msg->payload, expected_len)) {
                std::cerr << "Child: Data corruption for size " << expected_len << std::endl;
                ring.Release(msg);
                close(pipe_fd[0]);
                return 1;
            }

            std::cout << "Child: Verified " << expected_len << " bytes OK." << std::endl;
            ring.Release(msg);

            // 通知父进程已接收完毕
            char ack = 1;
            if (write(pipe_fd[0], &ack, 1) != 1) {
                perror("child write");
                close(pipe_fd[0]);
                return 1;
            }
        }

        close(pipe_fd[0]);
        std::cout << "Child: All messages verified, exiting.\n";
        return 0;

    } else {
        // ---------- 父进程：生产者 ----------
        close(pipe_fd[0]); // 关闭读端
        SharedMemoryRingBuffer ring;
        if (!ring.Create(shm_name, SHM_CAPACITY)) {
            std::cerr << "Parent: Create failed\n";
            close(pipe_fd[1]);
            return 1;
        }
        std::cout << "Parent: Created shared memory (capacity = " << SHM_CAPACITY << " bytes)\n";

        // 准备发送缓冲区（最大 1 MB）
        std::vector<uint8_t> buffer(1024 * 1024); // 1 MB
        bool all_ok = true;

        for (size_t len : test_sizes) {
            // 填充模式数据
            fill_pattern(buffer.data(), len);

            // 发送
            if (!ring.Send(buffer.data(), len)) {
                std::cerr << "Parent: Send failed for size " << len << std::endl;
                all_ok = false;
                break;
            }
            std::cout << "Parent: Sent " << len << " bytes." << std::endl;

            // 等待子进程确认
            char ack;
            if (read(pipe_fd[1], &ack, 1) != 1) {
                perror("parent read");
                all_ok = false;
                break;
            }
        }

        // 等待子进程退出
        int status;
        waitpid(pid, &status, 0);

        // 清理共享内存
        ring.Destroy();
        close(pipe_fd[1]);
        std::cout << "Parent: Done." << std::endl;
        return all_ok ? 0 : 1;
    }
}

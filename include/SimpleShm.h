#ifndef SIMPLE_SHM_H
#define SIMPLE_SHM_H

#include "SharedMemoryRingBuffer.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <thread>

// 简洁的生产者封装
class ShmPublisher {
public:
    // 构造时自动打开或创建共享内存
    // name: 共享内存名称（如 "/myshm"）
    // size: 数据区大小（仅当需要创建时使用，0 表示仅打开，失败则抛异常？这里返回 bool）
    // auto_create: 如果打开失败是否尝试创建
    ShmPublisher(const std::string& name, size_t size = 1024*1024, bool auto_create = true);
    ~ShmPublisher();

    // 禁止拷贝
    ShmPublisher(const ShmPublisher&) = delete;
    ShmPublisher& operator=(const ShmPublisher&) = delete;

    // 发送数据块
    // data: 载荷指针
    // len: 载荷字节数
    // filename: 可选的元数据文件名（用于溯源），传 nullptr 表示无
    // timeout_ms: 等待空间的时间（毫秒），-1 表示无限，0 表示立即返回
    // 返回: true 成功，false 超时或失败
    bool publish(const void* data, size_t len,
                 const char* filename = nullptr,
                 int timeout_ms = 100);

    // 获取最后一条消息的序列号
    uint64_t lastSequence() const;

private:
    SharedMemoryRingBuffer ring_;
    std::string name_;
};

// 简洁的消费者封装
class ShmSubscriber {
public:
    ShmSubscriber(const std::string& name);
    ~ShmSubscriber();

    ShmSubscriber(const ShmSubscriber&) = delete;
    ShmSubscriber& operator=(const ShmSubscriber&) = delete;

    bool receive(std::vector<uint8_t>& data,
                 std::string* filename = nullptr,
                 int timeout_ms = -1);

    void startMonitor(std::function<void(size_t queue_size, double msg_per_sec)> callback);
    void stopMonitor();

    // 新增：重置共享内存缓冲区，用于打破死锁
    void resetBuffer();

private:
    SharedMemoryRingBuffer ring_;
    std::atomic<bool> monitor_running_{false};
    std::thread monitor_thread_;
    void monitorLoop(std::function<void(size_t, double)> callback);
};

#endif // SIMPLE_SHM_H

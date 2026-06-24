#include "SimpleShm.h"
#include <iostream>
#include <sys/mman.h>   // shm_unlink

// ---------- ShmPublisher ----------
ShmPublisher::ShmPublisher(const std::string& name, size_t size, bool auto_create)
    : name_(name)
{
    if (!ring_.Open(name)) {
        if (auto_create) {
            // 打开失败，尝试创建（先清理可能残留的同名对象）
            shm_unlink(name.c_str());
            if (size < 4096) size = 4096;
            // 确保容量是 2 的幂（Create 内部会做）
            if (!ring_.Create(name, size)) {
                throw std::runtime_error("Failed to create shared memory: " + name);
            }
        } else {
            throw std::runtime_error("Failed to open shared memory: " + name);
        }
    }
}

ShmPublisher::~ShmPublisher() {
    // 仅解除映射，不删除共享内存（由创建者负责或允许泄漏）
}

bool ShmPublisher::publish(const void* data, size_t len,
                           const char* filename, int timeout_ms)
{
    return ring_.TrySend(data, len, timeout_ms, filename);
}

uint64_t ShmPublisher::lastSequence() const {
    return ring_.GetLastSeq();
}

// ---------- ShmSubscriber ----------
ShmSubscriber::ShmSubscriber(const std::string& name)
{
    int retry = 0;
    while (!ring_.Open(name)) {
        if (++retry > 300) { // 30 秒超时
            throw std::runtime_error("Failed to open shared memory: " + name);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

ShmSubscriber::~ShmSubscriber() {
    stopMonitor();
}

bool ShmSubscriber::receive(std::vector<uint8_t>& data,
                            std::string* filename,
                            int timeout_ms)
{
    std::string fname_tmp;
    bool ok = ring_.Receive(data, fname_tmp, timeout_ms);
    if (filename) {
        *filename = std::move(fname_tmp);
    }
    return ok;
}

void ShmSubscriber::startMonitor(std::function<void(size_t, double)> callback) {
    if (monitor_running_.exchange(true)) return; // 已经在运行
    monitor_thread_ = std::thread(&ShmSubscriber::monitorLoop, this, callback);
}

void ShmSubscriber::stopMonitor() {
    monitor_running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void ShmSubscriber::monitorLoop(std::function<void(size_t, double)> callback) {
    size_t last_count = 0;
    auto last_time = std::chrono::steady_clock::now();
    while (monitor_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!monitor_running_) break;
        // 统计当前就绪消息数（轻量，不会消费）
        size_t cur_count = 0;
        ring_.ListAllMessages([&](const Message*) { ++cur_count; });
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        size_t diff = (cur_count >= last_count) ? (cur_count - last_count) : 0;
        double rate = dt > 1e-6 ? diff / dt : 0.0;
        callback(cur_count, rate);
        last_count = cur_count;
        last_time = now;
    }
}

void ShmSubscriber::resetBuffer() {
    ring_.Reset();
}

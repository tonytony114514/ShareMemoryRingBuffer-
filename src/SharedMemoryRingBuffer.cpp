#include "SharedMemoryRingBuffer.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <algorithm>

static const uint64_t MAGIC_NUMBER = 0xDEADBEEFCAFEBABEULL;
static const int DEFAULT_TIMEOUT_MS = 100;

// ---- MessageToken ----
void MessageToken::Release() {
    if (ring_ && msg_) {
        ring_->ReleaseToken(msg_);
        msg_ = nullptr; ring_ = nullptr;
    }
}

// ---- SharedMemoryRingBuffer ----
SharedMemoryRingBuffer::SharedMemoryRingBuffer()
    : shm_fd_(-1), mapped_addr_(nullptr), meta_(nullptr),
      data_area_(nullptr), capacity_(0), is_creator_(false),
      last_sent_seq_(0) {}

SharedMemoryRingBuffer::~SharedMemoryRingBuffer() {
    if (mapped_addr_ != nullptr) {
        munmap(mapped_addr_, sizeof(SharedMeta) + capacity_);
        mapped_addr_ = nullptr;
    }
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool SharedMemoryRingBuffer::Destroy() {
    if (!is_creator_) return false;
    if (mapped_addr_ != nullptr) {
        munmap(mapped_addr_, sizeof(SharedMeta) + capacity_);
        mapped_addr_ = nullptr;
    }
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    if (!shm_name_.empty()) {
        shm_unlink(shm_name_.c_str());
    }
    return true;
}

bool SharedMemoryRingBuffer::Create(const std::string& name, size_t size) {
    if (name.empty() || size < 4096) return false;
    if (!is_power_of_two(size)) {
        size_t pow = 1;
        while (pow < size) pow <<= 1;
        size = pow;
    }
    shm_name_ = name;
    capacity_ = size;
    is_creator_ = true;

    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd_ == -1) return false;

    size_t total_size = sizeof(SharedMeta) + capacity_;
    if (ftruncate(shm_fd_, total_size) == -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }

    mapped_addr_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (mapped_addr_ == MAP_FAILED) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
        return false;
    }

    meta_ = reinterpret_cast<SharedMeta*>(mapped_addr_);
    data_area_ = reinterpret_cast<uint8_t*>(meta_) + sizeof(SharedMeta);
    init_meta(capacity_);
    return true;
}

bool SharedMemoryRingBuffer::Open(const std::string& name) {
    if (name.empty()) return false;
    shm_name_ = name;
    is_creator_ = false;

    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    if (shm_fd_ == -1) return false;

    struct stat st;
    if (fstat(shm_fd_, &st) == -1) {
        close(shm_fd_);
        return false;
    }
    size_t total_size = st.st_size;
    if (total_size < sizeof(SharedMeta)) {
        close(shm_fd_);
        return false;
    }

    mapped_addr_ = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (mapped_addr_ == MAP_FAILED) {
        close(shm_fd_);
        return false;
    }

    meta_ = reinterpret_cast<SharedMeta*>(mapped_addr_);
    capacity_ = meta_->capacity;
    data_area_ = reinterpret_cast<uint8_t*>(meta_) + sizeof(SharedMeta);

    if (meta_->magic != MAGIC_NUMBER) {
        munmap(mapped_addr_, total_size);
        close(shm_fd_);
        return false;
    }
    return true;
}

void SharedMemoryRingBuffer::init_meta(size_t cap) {
    meta_->writeIndex.store(0, std::memory_order_relaxed);
    meta_->readIndex.store(0, std::memory_order_relaxed);
    meta_->capacity = cap;
    meta_->magic = MAGIC_NUMBER;
    meta_->next_seq.store(0, std::memory_order_relaxed);
    meta_->read_seq.store(0, std::memory_order_relaxed);
    std::memset(meta_->padding, 0, sizeof(meta_->padding));
}

// ------------------------- 发送 -------------------------
bool SharedMemoryRingBuffer::Send(const void* data, size_t len) {
    return TrySend(data, len, DEFAULT_TIMEOUT_MS, nullptr);
}
bool SharedMemoryRingBuffer::SendFile(const char* filename, const void* data, size_t len) {
    return TrySend(data, len, DEFAULT_TIMEOUT_MS, filename);
}

bool SharedMemoryRingBuffer::TrySend(const void* data, size_t len, int timeout_ms, const char* filename) {
    if (data == nullptr || len == 0 || len > capacity_ / 2) return false;

    size_t name_len = filename ? strlen(filename) : 0;
    size_t total = message_total_size(len, name_len);
    if (total > capacity_) return false;

    auto start = std::chrono::steady_clock::now();
    while (true) {
        size_t read_idx = meta_->readIndex.load(std::memory_order_acquire);
        size_t write_idx = meta_->writeIndex.load(std::memory_order_acquire);
        size_t used = write_idx - read_idx;
        size_t free_space = capacity_ - used;

        if (free_space >= total) {
            size_t offset = write_idx % capacity_;
            size_t tail_space = capacity_ - offset;

            if (tail_space >= total) {
                if (meta_->writeIndex.compare_exchange_weak(write_idx, write_idx + total,
                            std::memory_order_release, std::memory_order_acquire)) {
                    uint8_t* write_ptr = data_area_ + offset;
                    uint64_t seq = meta_->next_seq.fetch_add(1, std::memory_order_relaxed);
                    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(write_ptr);
                    hdr->magic = MAGIC_NUMBER;
                    hdr->length = len;
                    hdr->timestamp = now;
                    hdr->seq = seq;
                    hdr->checksum = 0;
                    hdr->name_len = name_len;
                    hdr->reserved = 0;
                    hdr->ready = 0;
                    hdr->claimed = 0;
                    std::memset(hdr->padding, 0, sizeof(hdr->padding));

                    uint8_t* payload_dst = write_ptr + sizeof(MessageHeader);
                    if (filename) {
                        std::memcpy(payload_dst, filename, name_len);
                        payload_dst += name_len;
                    }
                    std::memcpy(payload_dst, data, len);

                    last_sent_seq_.store(seq, std::memory_order_relaxed);
                    __atomic_store_n(&hdr->ready, 1, __ATOMIC_RELEASE);
                    return true;
                }
                continue;
            }
            else {
                if (read_idx == write_idx) {
                    size_t pad_size = tail_space;
                    if (meta_->writeIndex.compare_exchange_weak(write_idx, write_idx + pad_size,
                                std::memory_order_release, std::memory_order_acquire)) {
                        std::memset(data_area_ + offset, 0, pad_size);
                        continue;
                    }
                    continue;
                }
            }
        }

        if (timeout_ms == 0) return false;
        if (timeout_ms < 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeout_ms)
            return false;
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

// ------------------------- 跳过空洞（安全版） -------------------------
void SharedMemoryRingBuffer::SkipHoles() {
    while (true) {
        size_t read_idx = meta_->readIndex.load(std::memory_order_acquire);
        size_t write_idx = meta_->writeIndex.load(std::memory_order_acquire);
        if (read_idx >= write_idx) return;

        size_t offset = read_idx % capacity_;
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(data_area_ + offset);

        uint64_t magic = hdr->magic;
        uint64_t len = hdr->length;
        uint32_t name_len = hdr->name_len;
        uint8_t ready_val = __atomic_load_n(&hdr->ready, __ATOMIC_ACQUIRE);

        // 空洞条件：魔数错、未就绪、载荷或文件名长度异常
        bool is_hole = (magic != MAGIC_NUMBER) || (ready_val == 0)
                       || (len > capacity_ / 2) || (name_len > capacity_ / 2);

        if (is_hole) {
            // 计算跳过长度，若不可信则使用最小跳过量 64 字节
            size_t skip_total = message_total_size(len, name_len);
            if (skip_total == 0 || skip_total > capacity_) {
                skip_total = 64;
            }

            // 关键：跳过长度不能超过当前剩余可读范围 (write_idx - read_idx)
            size_t remaining = write_idx - read_idx;
            if (skip_total > remaining) {
                skip_total = remaining;   // 安全地跳到 write_idx
            }

            // CAS 推进 readIndex
            if (meta_->readIndex.compare_exchange_weak(read_idx, read_idx + skip_total,
                        std::memory_order_release, std::memory_order_acquire)) {
                continue;  // 成功跳过，继续检查下一个
            }
            // CAS 失败，说明其他消费者修改了索引，重新循环
            continue;
        }
        break; // 遇到有效消息，停止跳过
    }
}

// ------------------------- 接收（安全拷贝） -------------------------
bool SharedMemoryRingBuffer::Receive(std::vector<uint8_t>& data, std::string& filename, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        SkipHoles();

        size_t read_idx = meta_->readIndex.load(std::memory_order_acquire);
        size_t write_idx = meta_->writeIndex.load(std::memory_order_acquire);
        if (read_idx >= write_idx) {
            if (timeout_ms == 0) return false;
            if (timeout_ms < 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeout_ms)
                return false;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        size_t offset = read_idx % capacity_;
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(data_area_ + offset);

        uint8_t ready_val = __atomic_load_n(&hdr->ready, __ATOMIC_ACQUIRE);
        if (hdr->magic != MAGIC_NUMBER || ready_val == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }

        // 再次验证长度，防止竞态
        if (hdr->length > capacity_ / 2 || hdr->name_len > capacity_ / 2) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }

        size_t total = message_total_size(hdr->length, hdr->name_len);
        // 确保 total 不会超过写指针（剩余可读）
        size_t remaining = write_idx - read_idx;
        if (total > remaining) {
            // 异常，跳过
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }

        if (meta_->readIndex.compare_exchange_weak(read_idx, read_idx + total,
                    std::memory_order_release, std::memory_order_acquire)) {
            // 校验物理边界：offset + total <= capacity_ （理论已满足）
            const uint8_t* payload_ptr = reinterpret_cast<const uint8_t*>(hdr) + sizeof(MessageHeader) + hdr->name_len;
            data.assign(payload_ptr, payload_ptr + hdr->length);
            if (hdr->name_len > 0) {
                const char* fname = reinterpret_cast<const char*>(hdr) + sizeof(MessageHeader);
                filename.assign(fname, hdr->name_len);
            } else {
                filename.clear();
            }
            return true;
        }
        // CAS 失败，继续
    }
}

bool SharedMemoryRingBuffer::Receive(std::vector<uint8_t>& data, int timeout_ms) {
    std::string dummy;
    return Receive(data, dummy, timeout_ms);
}

// ------------------------- 状态 -------------------------
bool SharedMemoryRingBuffer::IsEmpty() const {
    return meta_->readIndex.load(std::memory_order_acquire) == meta_->writeIndex.load(std::memory_order_acquire);
}
bool SharedMemoryRingBuffer::IsFull() const {
    size_t r = meta_->readIndex.load(std::memory_order_acquire);
    size_t w = meta_->writeIndex.load(std::memory_order_acquire);
    return (w - r) == capacity_;
}
size_t SharedMemoryRingBuffer::AvailableRead() const {
    return meta_->writeIndex.load(std::memory_order_acquire) - meta_->readIndex.load(std::memory_order_acquire);
}
size_t SharedMemoryRingBuffer::AvailableWrite() const {
    return capacity_ - (meta_->writeIndex.load(std::memory_order_acquire) - meta_->readIndex.load(std::memory_order_acquire));
}
size_t SharedMemoryRingBuffer::Capacity() const { return capacity_; }
void SharedMemoryRingBuffer::Reset() {
    meta_->writeIndex.store(0, std::memory_order_release);
    meta_->readIndex.store(0, std::memory_order_release);
    meta_->next_seq.store(0, std::memory_order_release);
}

void SharedMemoryRingBuffer::ListAllMessages(std::function<void(const Message*)> callback) const {
    size_t read_idx = meta_->readIndex.load(std::memory_order_acquire);
    size_t write_idx = meta_->writeIndex.load(std::memory_order_acquire);
    while (read_idx < write_idx) {
        size_t offset = read_idx % capacity_;
        MessageHeader* hdr = reinterpret_cast<MessageHeader*>(data_area_ + offset);
        uint64_t magic = hdr->magic;
        uint64_t len = hdr->length;
        uint32_t name_len = hdr->name_len;
        uint8_t ready_val = __atomic_load_n(&hdr->ready, __ATOMIC_ACQUIRE);

        if (magic != MAGIC_NUMBER || ready_val == 0 || len > capacity_ / 2 || name_len > capacity_ / 2) {
            size_t total = message_total_size(len, name_len);
            if (total == 0 || total > capacity_) total = 64;
            size_t remaining = write_idx - read_idx;
            if (total > remaining) total = remaining;
            read_idx += total;
            continue;
        }
        callback(reinterpret_cast<const Message*>(hdr));
        read_idx += message_total_size(len, name_len);
    }
}

// ------------------------- 辅助 -------------------------
size_t SharedMemoryRingBuffer::align_up(size_t n) const {
    return (n + 63) & ~size_t(63);
}
size_t SharedMemoryRingBuffer::message_total_size(size_t payload_len, size_t name_len) const {
    return align_up(sizeof(MessageHeader) + name_len + payload_len);
}
bool SharedMemoryRingBuffer::is_power_of_two(size_t n) const {
    return n && ((n & (n - 1)) == 0);
}

void SharedMemoryRingBuffer::ReleaseToken(const Message* msg) {
    if (!msg) return;
    size_t total = message_total_size(msg->header.length, msg->header.name_len);
    while (true) {
        size_t read_idx = meta_->readIndex.load(std::memory_order_acquire);
        if (data_area_ + (read_idx % capacity_) == reinterpret_cast<const uint8_t*>(msg)) {
            if (meta_->readIndex.compare_exchange_weak(read_idx, read_idx + total,
                        std::memory_order_release, std::memory_order_acquire)) break;
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

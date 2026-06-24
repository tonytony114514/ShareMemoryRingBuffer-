#ifndef SHARED_MEMORY_RING_BUFFER_H
#define SHARED_MEMORY_RING_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <atomic>
#include <functional>
#include <vector>

struct MessageHeader {
    uint64_t magic;          // 0xDEADBEEFCAFEBABE
    uint64_t length;         // payload length
    uint64_t timestamp;      // microsec
    uint64_t seq;
    uint32_t checksum;
    uint32_t name_len;
    uint32_t reserved;
    uint8_t  ready;          // atomic access
    uint8_t  claimed;
    uint8_t  padding[18];
};
static_assert(sizeof(MessageHeader) == 64, "MessageHeader must be 64 bytes");

struct Message {
    MessageHeader header;
    uint8_t payload[];       // filename + data + alignment
};

class SharedMemoryRingBuffer {
public:
    SharedMemoryRingBuffer();
    ~SharedMemoryRingBuffer();

    SharedMemoryRingBuffer(const SharedMemoryRingBuffer&) = delete;
    SharedMemoryRingBuffer& operator=(const SharedMemoryRingBuffer&) = delete;

    bool Create(const std::string& name, size_t size);
    bool Open(const std::string& name);
    bool Destroy();

    bool Send(const void* data, size_t len);
    bool SendFile(const char* filename, const void* data, size_t len);
    bool TrySend(const void* data, size_t len, int timeout_ms, const char* filename = nullptr);

    bool Receive(std::vector<uint8_t>& data, std::string& filename, int timeout_ms = -1);
    bool Receive(std::vector<uint8_t>& data, int timeout_ms = -1);

    bool IsEmpty() const;
    bool IsFull() const;
    size_t AvailableRead() const;
    size_t AvailableWrite() const;
    size_t Capacity() const;
    void Reset();
    uint64_t GetLastSeq() const { return last_sent_seq_.load(); }

    void ListAllMessages(std::function<void(const Message*)> callback) const;

    static const uint8_t* GetPayload(const Message* msg) {
        return reinterpret_cast<const uint8_t*>(msg) + sizeof(MessageHeader) + msg->header.name_len;
    }
    static const char* GetFileName(const Message* msg) {
        return reinterpret_cast<const char*>(msg) + sizeof(MessageHeader);
    }

private:
    struct SharedMeta {
        std::atomic<size_t> writeIndex;
        std::atomic<size_t> readIndex;
        size_t capacity;
        uint64_t magic;
        std::atomic<uint64_t> next_seq;
        std::atomic<uint64_t> read_seq;
        uint8_t padding[16];
    };
    static_assert(sizeof(SharedMeta) == 64, "SharedMeta must be 64 bytes");

    std::string shm_name_;
    int shm_fd_;
    void* mapped_addr_;
    SharedMeta* meta_;
    uint8_t* data_area_;
    size_t capacity_;
    bool is_creator_;
    std::atomic<uint64_t> last_sent_seq_;

    size_t align_up(size_t n) const;
    size_t message_total_size(size_t payload_len, size_t name_len) const;
    bool is_power_of_two(size_t n) const;
    void init_meta(size_t cap);
    void SkipHoles();
    void ReleaseToken(const Message* msg);
    friend class MessageToken;
};

// Optional zero-copy token
class MessageToken {
public:
    MessageToken() : msg_(nullptr), ring_(nullptr) {}
    ~MessageToken() { Release(); }
    MessageToken(const MessageToken&) = delete;
    MessageToken& operator=(const MessageToken&) = delete;
    MessageToken(MessageToken&& other) noexcept : msg_(other.msg_), ring_(other.ring_) {
        other.msg_ = nullptr; other.ring_ = nullptr;
    }
    MessageToken& operator=(MessageToken&& other) noexcept {
        if (this != &other) { Release(); msg_ = other.msg_; ring_ = other.ring_; other.msg_ = nullptr; other.ring_ = nullptr; }
        return *this;
    }
    const Message* operator->() const { return msg_; }
    const Message& operator*()  const { return *msg_; }
    explicit operator bool() const { return msg_ != nullptr; }
    void Release();

private:
    friend class SharedMemoryRingBuffer;
    MessageToken(const Message* msg, SharedMemoryRingBuffer* ring) : msg_(msg), ring_(ring) {}
    const Message* msg_;
    SharedMemoryRingBuffer* ring_;
};

#endif

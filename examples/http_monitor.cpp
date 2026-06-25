#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "SharedMemoryRingBuffer.h"

// 简单的 HTTP 请求解析
struct HttpRequest {
    std::string method;
    std::string path;
};

// 从 HTTP 请求字符串中解析方法、路径
HttpRequest parseRequest(const std::string& request) {
    HttpRequest req;
    size_t pos = request.find(' ');
    if (pos != std::string::npos) {
        req.method = request.substr(0, pos);
        size_t pos2 = request.find(' ', pos + 1);
        if (pos2 != std::string::npos) {
            req.path = request.substr(pos + 1, pos2 - pos - 1);
        }
    }
    return req;
}

// 构建 JSON 字符串（手动，避免依赖第三方库）
std::string buildStatsJson(SharedMemoryRingBuffer& ring) {
    std::ostringstream json;
    json << "{";
    json << "\"capacity\":" << ring.Capacity() << ",";
    json << "\"readable_bytes\":" << ring.AvailableRead() << ",";
    json << "\"writable_bytes\":" << ring.AvailableWrite() << ",";
    json << "\"is_empty\":" << (ring.IsEmpty() ? "true" : "false") << ",";
    json << "\"is_full\":" << (ring.IsFull() ? "true" : "false");

    // 如果需要消息数量，可以遍历（注意这会消耗 CPU，谨慎使用）
    size_t msg_count = 0;
    ring.ListAllMessages([&](const Message*) { ++msg_count; });
    json << ",\"message_count\":" << msg_count;

    json << "}";
    return json.str();
}

// 处理单个客户端连接
void handleClient(int client_fd, SharedMemoryRingBuffer& ring) {
    char buffer[2048] = {0};
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    HttpRequest req = parseRequest(buffer);
    std::string response;

    if (req.path == "/stats" && req.method == "GET") {
        std::string json = buildStatsJson(ring);
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: " + std::to_string(json.size()) + "\r\n"
                   "Connection: close\r\n"
                   "\r\n" + json;
    } else if (req.path == "/reset" && req.method == "GET") {
        ring.Reset();
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 7\r\n"
                   "Connection: close\r\n"
                   "\r\nBuffer reset";
    } else if (req.path == "/" || req.path == "/help") {
        std::string msg = "Usage: GET /stats or GET /reset";
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: " + std::to_string(msg.size()) + "\r\n"
                   "Connection: close\r\n"
                   "\r\n" + msg;
    } else {
        std::string msg = "404 Not Found";
        response = "HTTP/1.1 404 Not Found\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: " + std::to_string(msg.size()) + "\r\n"
                   "Connection: close\r\n"
                   "\r\n" + msg;
    }

    send(client_fd, response.c_str(), response.size(), 0);
    close(client_fd);
}

int main(int argc, char* argv[]) {
    std::string shm_name = "/demo_shm";
    int port = 8080;

    if (argc >= 2) shm_name = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    // 打开共享内存（若不存在则等待）
    SharedMemoryRingBuffer ring;
    while (!ring.Open(shm_name)) {
        std::cerr << "[HTTP] Waiting for shared memory " << shm_name << "..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cerr << "[HTTP] Connected to " << shm_name << std::endl;

    // 创建 TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "[HTTP] Listening on port " << port << std::endl;

    // 主循环：接受连接并处理
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        // 单线程处理，简单场景足够
        handleClient(client_fd, ring);
    }

    close(server_fd);
    return 0;
}

#include "SharedMemoryRingBuffer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <cstring>
#include <sys/mman.h>   // for shm_unlink

using namespace std;

static atomic<bool> running{true};
static void sigint_handler(int) { running = false; }

static void print_help() {
    cout << "\nAvailable commands:\n";
    cout << "  help                     - Show this help\n";
    cout << "  open <name>              - Open shared memory\n";
    cout << "  create <name> <size>     - Create shared memory (size in bytes)\n";
    cout << "  close                    - Close current shared memory\n";
    cout << "  send <payload>           - Send text payload\n";
    cout << "  send_file <filename>     - Send payload from file (content only)\n";
    cout << "  recv_one                 - Receive one message (blocking, 1s timeout)\n";
    cout << "  recv_a                   - Receive all messages continuously (Ctrl+C to stop)\n";
    cout << "  list                     - List all messages without consuming\n";
    cout << "  download <seq> <out>     - Download a specific message (by seq) to file\n";
    cout << "  dump <n>                 - Dump first n messages to screen (hex)\n";
    cout << "  monitor                  - Live monitor (message count and throughput per second)\n";
    cout << "  stats                    - Show current buffer statistics\n";
    cout << "  reset                    - Reset buffer pointers\n";
    cout << "  quit / exit              - Quit program\n\n";
}

static void list_messages(SharedMemoryRingBuffer& ring) {
    int count = 0;
    ring.ListAllMessages([&](const Message* msg) {
        cout << "Seq: " << msg->header.seq
             << ", size: " << msg->header.length
             << ", file: " << (msg->header.name_len ? SharedMemoryRingBuffer::GetFileName(msg) : "(none)")
             << ", timestamp: " << msg->header.timestamp << endl;
        ++count;
    });
    cout << "Total messages in buffer: " << count << endl;
}

static bool download_message(SharedMemoryRingBuffer& ring, uint64_t target_seq, const string& outfile) {
    bool found = false;
    ring.ListAllMessages([&](const Message* msg) {
        if (msg->header.seq == target_seq) {
            ofstream out(outfile, ios::binary);
            if (!out) {
                cerr << "Cannot open output file: " << outfile << endl;
                return;
            }
            const uint8_t* payload = SharedMemoryRingBuffer::GetPayload(msg);
            out.write(reinterpret_cast<const char*>(payload), msg->header.length);
            out.close();
            cout << "Downloaded message seq=" << target_seq
                 << " size=" << msg->header.length << " to " << outfile << endl;
            found = true;
        }
    });
    return found;
}

static void dump_messages(SharedMemoryRingBuffer& ring, int n) {
    int count = 0;
    ring.ListAllMessages([&](const Message* msg) {
        if (n > 0 && count >= n) return;
        cout << "--- Message seq=" << msg->header.seq
             << " size=" << msg->header.length
             << " file=" << (msg->header.name_len ? SharedMemoryRingBuffer::GetFileName(msg) : "(none)")
             << " ---" << endl;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(msg);
        size_t total = sizeof(MessageHeader) + msg->header.name_len + msg->header.length;
        total = ((total + 63) / 64) * 64; // aligned size
        for (size_t i = 0; i < total; ++i) {
            if (i % 16 == 0) cout << setw(8) << setfill('0') << hex << i << ": ";
            cout << setw(2) << setfill('0') << hex << (int)p[i] << " ";
            if (i % 16 == 15) cout << endl;
        }
        if (total % 16) cout << endl;
        cout << dec;
        ++count;
    });
    cout << "Dumped " << count << " messages." << endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);

    SharedMemoryRingBuffer ring;
    bool opened = false;

    // If a name is provided as argument, auto-open
    if (argc >= 2) {
        if (ring.Open(argv[1])) {
            opened = true;
            cout << "Auto-opened " << argv[1] << endl;
        } else {
            cerr << "Failed to auto-open " << argv[1] << endl;
        }
    }

    string line;
    cout << "Shared Memory Ring Buffer CLI (MPMC). Type 'help' for commands.\n";

    while (running) {
        if (opened) {
            cout << "[" << (argc >= 2 ? argv[1] : "shm") << "]> " << flush;
        } else {
            cout << "[no shm]> " << flush;
        }
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        istringstream iss(line);
        string cmd;
        iss >> cmd;

        if (cmd == "help") {
            print_help();
        }
        else if (cmd == "quit" || cmd == "exit") {
            running = false;
        }
        else if (cmd == "open") {
            string name;
            if (!(iss >> name)) { cerr << "Usage: open <name>\n"; continue; }
            if (opened) { cout << "Already open. Use close first.\n"; continue; }
            if (ring.Open(name)) {
                opened = true;
                cout << "Opened " << name << endl;
            } else {
                cerr << "Failed to open " << name << endl;
            }
        }
        else if (cmd == "create") {
            string name;
            size_t size;
            if (!(iss >> name >> size)) { cerr << "Usage: create <name> <size>\n"; continue; }
            if (opened) { cout << "Already open. Use close first.\n"; continue; }
            shm_unlink(name.c_str());
            if (ring.Create(name, size)) {
                opened = true;
                cout << "Created " << name << " size=" << size << endl;
            } else {
                cerr << "Failed to create " << name << endl;
            }
        }
        else if (cmd == "close") {
            if (!opened) { cout << "No shared memory open.\n"; continue; }
            ring.Destroy();  // will unmap and unlink if creator
            opened = false;
            cout << "Closed.\n";
        }
        else if (cmd == "send") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            string payload;
            getline(iss, payload);
            if (!payload.empty() && payload[0] == ' ') payload.erase(0,1);
            if (ring.Send(payload.data(), payload.size())) {
                cout << "Sent " << payload.size() << " bytes, seq=" << ring.GetLastSeq() << endl;
            } else {
                cerr << "Send failed.\n";
            }
        }
        else if (cmd == "send_file") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            string filename;
            if (!(iss >> filename)) { cerr << "Usage: send_file <filename>\n"; continue; }
            ifstream in(filename, ios::binary | ios::ate);
            if (!in) { cerr << "Cannot open file: " << filename << endl; continue; }
            streamsize size = in.tellg();
            in.seekg(0);
            vector<char> buffer(size);
            if (!in.read(buffer.data(), size)) { cerr << "Read error.\n"; continue; }
            if (ring.TrySend(buffer.data(), size, 1000, filename.c_str())) {
                cout << "Sent file " << filename << " (" << size << " bytes), seq=" << ring.GetLastSeq() << endl;
            } else {
                cerr << "Send file failed.\n";
            }
        }
        else if (cmd == "recv_one") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            vector<uint8_t> data;
            string fname;
            if (ring.Receive(data, fname, 1000)) {   // 1 second timeout
                cout << "Received: size=" << data.size()
                     << ", file=" << (fname.empty() ? "(none)" : fname) << endl;
            } else {
                cout << "No message available (timeout).\n";
            }
        }
        else if (cmd == "recv_a") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            cout << "Receiving all messages (Ctrl+C to stop)...\n";
            size_t total = 0;
            auto start = chrono::steady_clock::now();
            vector<uint8_t> data;
            string fname;
            while (running) {
                if (ring.Receive(data, fname, 100)) {   // 100ms timeout
                    ++total;
                }
            }
            auto end = chrono::steady_clock::now();
            double sec = chrono::duration<double>(end - start).count();
            cout << "Received " << total << " messages in " << sec << "s ("
                 << (sec>0 ? total/sec : 0) << " msg/s)" << endl;
            running = true; // reset for further commands
        }
        else if (cmd == "list") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            list_messages(ring);
        }
        else if (cmd == "download") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            uint64_t seq;
            string outfile;
            if (!(iss >> seq >> outfile)) { cerr << "Usage: download <seq> <output_file>\n"; continue; }
            if (!download_message(ring, seq, outfile)) {
                cerr << "Message seq=" << seq << " not found.\n";
            }
        }
        else if (cmd == "dump") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            int n = 10;
            iss >> n;
            if (n <= 0) n = 10;
            dump_messages(ring, n);
        }
        // ---------------- 修复后的 monitor 命令 ----------------
        else if (cmd == "monitor") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            cout << "Monitoring (Ctrl+C to stop)...\n";
            size_t last_msg_count = 0;
            auto last_time = chrono::steady_clock::now();
            while (running) {
                this_thread::sleep_for(chrono::seconds(1));
                if (!running) break;

                // 统计当前就绪消息数量
                size_t cur_msg_count = 0;
                ring.ListAllMessages([&](const Message*) { ++cur_msg_count; });

                auto now = chrono::steady_clock::now();
                double dt = chrono::duration<double>(now - last_time).count();
                // 避免因重置导致的负值
                size_t diff = (cur_msg_count >= last_msg_count) ? (cur_msg_count - last_msg_count) : 0;
                double rate = (dt > 1e-6) ? diff / dt : 0.0;

                cout << "Queue size: " << cur_msg_count
                     << " msgs | throughput: " << rate << " msg/s" << endl;

                last_msg_count = cur_msg_count;
                last_time = now;
            }
            running = true; // 允许继续使用 CLI
        }
        else if (cmd == "stats") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            cout << "Capacity: " << ring.Capacity() << " bytes\n";
            cout << "Readable bytes: " << ring.AvailableRead() << endl;
            cout << "Writable bytes: " << ring.AvailableWrite() << endl;
            cout << "Empty: " << (ring.IsEmpty() ? "yes" : "no") << endl;
            cout << "Full: " << (ring.IsFull() ? "yes" : "no") << endl;
        }
        else if (cmd == "reset") {
            if (!opened) { cerr << "No shared memory open.\n"; continue; }
            ring.Reset();
            cout << "Buffer reset.\n";
        }
        else {
            cerr << "Unknown command: " << cmd << " (type 'help')\n";
        }
    }

    cout << "Exiting.\n";
    return 0;
}

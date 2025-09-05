// tcp_flush_bench.cpp
// Build: g++ -O2 -std=c++17 tcp_flush_bench.cpp -lpthread -o tcp_flush_bench
// Run:   ./tcp_flush_bench [num_msgs] [payload_bytes] [batch_size]
//        e.g. ./tcp_flush_bench 1000000 32 1000

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

// helper: start strace attached to current process
pid_t start_strace(const char* logfile) {
    pid_t pid = fork();
    if (pid == 0) {
        std::string parent_pid = std::to_string(getppid());
        execlp("strace", "strace",
               "-c",
               "-e", "trace=write,writev,sendto,sendmsg,epoll_wait",
               "-p", parent_pid.c_str(),
               "-o", logfile,
               (char*)nullptr);
        perror("execlp failed");
        _exit(1);
    }
    // give strace time to attach
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return pid;
}

// helper: stop strace
void stop_strace(pid_t spid) {
    kill(spid, SIGINT);
    waitpid(spid, nullptr, 0);
}

static int make_server(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::exit(1); }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); std::exit(1); }
    if (::listen(fd, 1) < 0) { perror("listen"); std::exit(1); }
    return fd;
}

static void drain_fd(int cfd) {
    std::vector<char> buf(1 << 20);
    for (;;) {
        ssize_t n = ::read(cfd, buf.data(), buf.size());
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
    }
}

static void server_thread_fn(uint16_t port, int accepts) {
    int lfd = make_server(port);
    for (int i = 0; i < accepts; ++i) {
        int cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd < 0) { perror("accept"); std::exit(1); }
        drain_fd(cfd);
        ::close(cfd);
    }
    ::close(lfd);
}

static int connect_client(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::exit(1); }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); // disable Nagle

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    // Retry up to ~5s, 10ms interval
    for (int i = 0; i < 500; ++i) {
        if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        if (errno == ECONNREFUSED || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        perror("connect");
        std::exit(1);
    }
    std::cerr << "timeout waiting for server\n";
    std::exit(1);
}

static void write_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("write");
            std::exit(1);
        }
        off += static_cast<size_t>(n);
    }
}

int main(int argc, char** argv) {
    const uint64_t num_msgs = (argc > 1) ? std::stoull(argv[1]) : 1000000ULL;
    const size_t   payload  = (argc > 2) ? static_cast<size_t>(std::stoull(argv[2])) : 100;
    const uint64_t batch_sz = (argc > 3) ? std::stoull(argv[3]) : 1000ULL;
    const uint16_t port = 55666;

    std::cout << "[client] PID=" << getpid()
              << " num_msgs=" << num_msgs
              << " payload=" << payload
              << " batch_sz=" << batch_sz
              << " port=" << port << "\n";

    // Server will accept 2 connections (per-message phase, then batched phase)
    std::thread srv(server_thread_fn, port, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // reduce connect race

    int cfd = connect_client(port);

    std::vector<char> msg(payload, 'x');
    std::vector<char> batch;
    batch.resize(static_cast<size_t>(batch_sz) * payload);
    for (uint64_t i = 0; i < batch_sz; ++i) {
        std::memcpy(batch.data() + static_cast<size_t>(i) * payload, msg.data(), payload);
    }

    // Per-message: one small write per message
    auto t1 = std::chrono::steady_clock::now();
    pid_t strace1 = start_strace("trace_phase1.log");
    for (uint64_t i = 0; i < num_msgs; ++i) {
        write_all(cfd, msg.data(), msg.size());
    }
    stop_strace(strace1);
    auto t2 = std::chrono::steady_clock::now();

    ::shutdown(cfd, SHUT_RDWR);
    ::close(cfd);

    // Batched: write batch_sz messages per write
    cfd = connect_client(port);

    auto t3 = std::chrono::steady_clock::now();
    pid_t strace2 = start_strace("trace_phase2.log");
    uint64_t sent = 0;
    while (sent < num_msgs) {
        uint64_t remaining = num_msgs - sent;
        uint64_t this_batch = std::min<uint64_t>(batch_sz, remaining);
        if (this_batch == batch_sz) {
            write_all(cfd, batch.data(), batch.size());
        } else {
            write_all(cfd, batch.data(), static_cast<size_t>(this_batch) * payload);
        }
        sent += this_batch;
    }
    stop_strace(strace2);
    auto t4 = std::chrono::steady_clock::now();

    ::shutdown(cfd, SHUT_RDWR);
    ::close(cfd);
    srv.join();

    auto per_ms = std::chrono::duration<double>(t2 - t1).count() * 1000.0;
    auto bat_ms = std::chrono::duration<double>(t4 - t3).count() * 1000.0;

    std::cout << "Messages: " << num_msgs
              << ", payload: " << payload << " bytes"
              << ", batch_sz: " << batch_sz << "\n";
    std::cout << "Per-message writes: " << per_ms << " ms total, "
              << (per_ms * 1000.0 / num_msgs) << " us/msg\n";
    std::cout << "Batched writes    : " << bat_ms << " ms total, "
              << (bat_ms * 1000.0 / num_msgs) << " us/msg\n";
    return 0;
}

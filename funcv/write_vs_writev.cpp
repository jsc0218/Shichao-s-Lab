// g++ -O2 -std=c++17 write_vs_writev.cpp -o write_vs_writev
// Benchmark using /dev/null
// ./write_vs_writev 4 nodisk
// Benchmark writing to disk (OS will buffer writes, no fsync)
// ./write_vs_writev 4 disk
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>

using namespace std;
using namespace std::chrono;

constexpr int BUF_SIZE = 1024;     // bytes per buffer
constexpr int ITER     = 100000;   // iterations for /dev/null
constexpr int ITER_DISK = 10000;   // iterations for disk (smaller to avoid large files)

void print_result(const string& name, double ms, double total_bytes) {
    double seconds = ms / 1000.0;
    double mb = total_bytes / (1024.0 * 1024.0);
    double mbps = mb / seconds;
    cout << name << " total time: " << ms << " ms, throughput: "
         << mbps << " MB/s" << endl;
}

void test_write(int fd, const vector<vector<char>>& bufs, int n_bufs, int iter) {
    auto start = steady_clock::now();
    for (int i = 0; i < iter; ++i) {
        for (int j = 0; j < n_bufs; ++j) {
            ssize_t written = write(fd, bufs[j].data(), bufs[j].size());
            if (written != (ssize_t)bufs[j].size()) {
                perror("write");
                exit(1);
            }
        }
    }
    auto end = steady_clock::now();
    double ms = duration_cast<milliseconds>(end - start).count();
    double total_bytes = (double)iter * n_bufs * BUF_SIZE;
    print_result("write()", ms, total_bytes);
}

void test_writev(int fd, const vector<vector<char>>& bufs, int n_bufs, int iter) {
    vector<iovec> iov(n_bufs);
    for (int i = 0; i < n_bufs; ++i) {
        iov[i].iov_base = (void*)bufs[i].data();
        iov[i].iov_len  = bufs[i].size();
    }

    auto start = steady_clock::now();
    for (int i = 0; i < iter; ++i) {
        ssize_t written = writev(fd, iov.data(), n_bufs);
        if (written < 0) {
            perror("writev");
            exit(1);
        }
    }
    auto end = steady_clock::now();
    double ms = duration_cast<milliseconds>(end - start).count();
    double total_bytes = (double)iter * n_bufs * BUF_SIZE;
    print_result("writev()", ms, total_bytes);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <num_buffers> <disk|nodisk>\n";
        return 1;
    }

    int n_bufs = atoi(argv[1]);
    if (n_bufs <= 0 || n_bufs > 1024) {
        cerr << "Error: num_buffers must be between 1 and 1024\n";
        return 1;
    }

    bool disk_mode = (string(argv[2]) == "disk");
    int iter = disk_mode ? ITER_DISK : ITER;

    cout << "Running benchmark with " << n_bufs
         << " buffers of " << BUF_SIZE << " bytes each, "
         << iter << " iterations, "
         << (disk_mode ? "disk file" : "/dev/null") << " mode.\n";

    vector<vector<char>> bufs(n_bufs, vector<char>(BUF_SIZE, 'x'));

    int fd;
    if (disk_mode) {
        fd = open("write_test.dat", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    } else {
        fd = open("/dev/null", O_WRONLY);
    }

    if (fd < 0) {
        perror("open");
        return 1;
    }

    test_write(fd, bufs, n_bufs, iter);
    test_writev(fd, bufs, n_bufs, iter);

    close(fd);
    return 0;
}

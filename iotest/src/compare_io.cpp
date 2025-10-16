// g++ -O2 -std=c++17 compare_io.cpp -o compare_io
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <cstring>
#include <cstdlib>

using namespace std;

constexpr size_t BLOCK_SIZE = 4096;
constexpr size_t FILE_SIZE  = 512 * 1024 * 1024; // 512 MB

constexpr const char* FILE_OS_BUFFERED  = "test_os_buffered_io.dat";
constexpr const char* FILE_DIRECT    = "test_direct_io.dat";
constexpr const char* FILE_USER_BUFFERED = "test_user_buffered_io.dat";

// Allocate aligned buffer (required for O_DIRECT)
void* aligned_alloc_block(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, BLOCK_SIZE, size) != 0) {
        cerr << "posix_memalign failed\n";
        exit(1);
    }
    memset(ptr, 'A', size);
    return ptr;
}

double write_os_buffered(const char* filename, void* buf, size_t total_size) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror("open (buffered)"); exit(1); }

    auto start = chrono::high_resolution_clock::now();

    size_t written = 0;
    while (written < total_size) {
        ssize_t n = write(fd, buf, BLOCK_SIZE);
        if (n < 0) { perror("write buffered"); exit(1); }
        written += n;
    }

    fsync(fd);
    auto end = chrono::high_resolution_clock::now();
    close(fd);

    return chrono::duration<double>(end - start).count();
}

double write_direct(const char* filename, void* buf, size_t total_size) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0666);
    if (fd < 0) { perror("open (direct)"); exit(1); }

    auto start = chrono::high_resolution_clock::now();

    size_t written = 0;
    while (written < total_size) {
        ssize_t n = write(fd, buf, BLOCK_SIZE);
        if (n < 0) { perror("write direct"); exit(1); }
        written += n;
    }

    fsync(fd);
    auto end = chrono::high_resolution_clock::now();
    close(fd);

    return chrono::duration<double>(end - start).count();
}

double write_user_buffered(const char* filename, void* buf, size_t total_size) {
    // Simulate user-space cache: accumulate in RAM first
    char* user_cache = (char*)malloc(total_size);
    if (!user_cache) { cerr << "malloc failed\n"; exit(1); }

    // Step 1: write all to user-space cache (fast memory copy)
    auto start = chrono::high_resolution_clock::now();
    for (size_t i = 0; i < total_size; i += BLOCK_SIZE) {
        memcpy(user_cache + i, buf, BLOCK_SIZE);
    }

    // Step 2: flush user cache -> OS page cache in one large write
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror("open (user cache)"); exit(1); }

    ssize_t n = write(fd, user_cache, total_size);
    if (n < 0) { perror("write (user cache flush)"); exit(1); }

    // Step 3: flush OS cache to disk
    fsync(fd);
    close(fd);

    auto end = chrono::high_resolution_clock::now();
    free(user_cache);

    return chrono::duration<double>(end - start).count();
}

int main() {
    cout << "Comparing OS Buffered, Direct, and User Buffered I/O (" 
         << FILE_SIZE / (1024*1024) << " MB)\n";

    void* buf = aligned_alloc_block(BLOCK_SIZE);

    double t1 = write_os_buffered(FILE_OS_BUFFERED, buf, FILE_SIZE);
    cout << "OS Buffered I/O:          " << t1 << " sec\n";

    double t2 = write_direct(FILE_DIRECT, buf, FILE_SIZE);
    cout << "Direct I/O:            " << t2 << " sec\n";

    double t3 = write_user_buffered(FILE_USER_BUFFERED, buf, FILE_SIZE);
    cout << "User Buffered I/O:  " << t3 << " sec\n";

    free(buf);
    return 0;
}

// g++ -O2 futex_vs_semaphore.cpp -o futex_vs_semaphore
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <atomic>
#include <chrono>
#include <iostream>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>

constexpr int ITER = 10'000'000;

// ---------------- Timing ----------------
double now_sec() {
    using namespace std::chrono;
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------- Futex helpers ----------------
static inline int futex_wait(std::atomic<int>* addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, nullptr, nullptr, 0);
}

static inline int futex_wake(std::atomic<int>* addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, nullptr, nullptr, 0);
}

// ---------------- Benchmark ----------------
void benchmark_futex() {
    // Shared memory for futex and counter
    auto shared_futex = (std::atomic<int>*)mmap(nullptr, sizeof(std::atomic<int>)*2,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    shared_futex[0].store(0); // lock
    shared_futex[1].store(0); // counter

    pid_t pid = fork();
    if (pid == 0) {
        // Child: increment counter
        for (int i = 0; i < ITER; i++) {
            // Wait until lock=0, then acquire
            int c = 0;
            while (!shared_futex[0].compare_exchange_weak(c, 1, std::memory_order_acquire)) {
                c = 0;
                futex_wait(&shared_futex[0], 1);
            }
            shared_futex[1]++;
            // Release lock
            shared_futex[0].store(0, std::memory_order_release);
            futex_wake(&shared_futex[0], 1);
        }
        exit(0);
    }

    // Parent: increment counter
    double start = now_sec();
    for (int i = 0; i < ITER; i++) {
        int c = 0;
        while (!shared_futex[0].compare_exchange_weak(c, 1, std::memory_order_acquire)) {
            c = 0;
            futex_wait(&shared_futex[0], 1);
        }
        shared_futex[1]++;
        shared_futex[0].store(0, std::memory_order_release);
        futex_wake(&shared_futex[0], 1);
    }

    wait(nullptr);
    double end = now_sec();
    std::cout << "Futex: " << (end - start) << " sec, counter=" << shared_futex[1].load() << "\n";
    munmap(shared_futex, sizeof(std::atomic<int>)*2);
}

// ---------------- POSIX Semaphore ----------------
void benchmark_semaphore() {
    sem_t* sem = sem_open("/mysem", O_CREAT | O_EXCL, 0666, 1);
    if (sem == SEM_FAILED) { perror("sem_open"); return; }

    // Shared memory counter
    int* counter = (int*)mmap(nullptr, sizeof(int),
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *counter = 0;

    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < ITER; i++) {
            sem_wait(sem);
            (*counter)++;
            sem_post(sem);
        }
        exit(0);
    }

    double start = now_sec();
    for (int i = 0; i < ITER; i++) {
        sem_wait(sem);
        (*counter)++;
        sem_post(sem);
    }

    wait(nullptr);
    double end = now_sec();
    std::cout << "POSIX Semaphore: " << (end - start) << " sec, counter=" << *counter << "\n";

    munmap(counter, sizeof(int));
    sem_close(sem);
    sem_unlink("/mysem");
}

// ---------------- Main ----------------
int main() {
    std::cout << "Comparing inter-process futex vs POSIX semaphore (" << ITER << " iterations each)\n";
    benchmark_futex();
    benchmark_semaphore();
    return 0;
}

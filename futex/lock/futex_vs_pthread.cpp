// g++ -O2 -pthread futex_vs_pthread.cpp -o futex_vs_pthread

#include <atomic>
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <thread>

constexpr int ITER = 10'000'000;

// ---------------- Timing helper ----------------
double now_sec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------------- Pthread mutex ----------------
pthread_mutex_t pmutex = PTHREAD_MUTEX_INITIALIZER;
int counter = 0;

void* pthread_worker(void* arg) {
    for (int i = 0; i < ITER; i++) {
        pthread_mutex_lock(&pmutex);
        counter++;
        pthread_mutex_unlock(&pmutex);
    }
    return nullptr;
}

void test_pthread() {
    counter = 0;
    pthread_t t;
    double start = now_sec();
    pthread_create(&t, nullptr, pthread_worker, nullptr);
    pthread_worker(nullptr);
    pthread_join(t, nullptr);
    double end = now_sec();
    std::cout << "pthread_mutex: " << (end - start) << " sec, counter=" << counter << "\n";
}

// ---------------- Futex-based mutex ----------------
class FutexMutex {
    std::atomic<int> lock_{0};

    static inline int futex_wait(std::atomic<int>* addr, int val) {
        return syscall(SYS_futex, addr, FUTEX_WAIT, val, nullptr, nullptr, 0);
    }

    static inline int futex_wake(std::atomic<int>* addr, int n) {
        return syscall(SYS_futex, addr, FUTEX_WAKE, n, nullptr, nullptr, 0);
    }

public:
    /*inline void lock() {
        int c = 0;
        //lock_.store(0, std::memory_order_release);
        while (!lock_.compare_exchange_weak(c, 1, std::memory_order_acquire)) {
              c =  0;
        //    futex_wait(&lock_, 0);
        }
    }

    inline void unlock() {
        lock_.store(1, std::memory_order_release);
        //futex_wake(&lock_, 1);
    }*/
    
    inline void lock() {
        int expected;
        while (true) {
            expected = 0;
            if (lock_.compare_exchange_weak(expected, 1, std::memory_order_acquire))
                return;
            //std::this_thread::yield(); // optional
            expected = 0;
            futex_wait(&lock_, 1);   // sleep until lock changes
        }
    }

    inline void unlock() {
        lock_.store(0, std::memory_order_release);
        futex_wake(&lock_, 1);      // wake one waiting thread
    }
};

FutexMutex fmutex;

void* futex_worker(void* arg) {
    for (int i = 0; i < ITER; i++) {
        fmutex.lock();
        counter++;
        fmutex.unlock();
    }
    return nullptr;
}

void test_futex() {
    counter = 0;
    double start = now_sec();
    pthread_t t;
    pthread_create(&t, nullptr, futex_worker, nullptr);
    futex_worker(nullptr);
    pthread_join(t, nullptr);
    double end = now_sec();
    std::cout << "futex_mutex: " << (end - start) << " sec, counter=" << counter << "\n";
}

// ---------------- Main ----------------
int main() {
    std::cout << "Comparing pthread_mutex vs futex-based mutex (" << ITER << " iterations)\n";
    test_pthread();
    test_futex();
    return 0;
}

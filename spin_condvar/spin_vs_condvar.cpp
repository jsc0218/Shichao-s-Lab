#include <iostream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>

constexpr int ITER = 100000;

void spinlock_test() {
    std::atomic<bool> ready(false);
    int counter = 0;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread worker([&]() {
        for (int i = 0; i < ITER; i++) {
            while (!ready.load(std::memory_order_acquire)) {
                // spin
            }
            ready.store(false, std::memory_order_release);
            counter++;
        }
    });

    for (int i = 0; i < ITER; i++) {
        ready.store(true, std::memory_order_release);
        while (ready.load(std::memory_order_acquire)) {
            // wait for worker reset
        }
    }

    worker.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double per_op = static_cast<double>(total_ns) / ITER;

    std::cout << "Spinlock average per-op: " << per_op << " ns\n";
}

void condvar_test() {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    int counter = 0;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread worker([&]() {
        for (int i = 0; i < ITER; i++) {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return ready; });
            ready = false;
            counter++;
            cv.notify_one();
        }
    });

    for (int i = 0; i < ITER; i++) {
        std::unique_lock<std::mutex> lock(m);
        ready = true;
        cv.notify_one();
        cv.wait(lock, [&] { return !ready; });
    }

    worker.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double per_op = static_cast<double>(total_ns) / ITER;

    std::cout << "Condition variable average per-op: " << per_op << " ns\n";
}

int main() {
    spinlock_test();
    condvar_test();
}

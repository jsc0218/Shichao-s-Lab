// g++ -O2 -pthread mutex_vs_atomic.cpp -o mutex_vs_atomic
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    long iterations = 1'000'000; // default 1 million
    if (argc > 1)
        iterations = std::stol(argv[1]);

    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, nullptr);

    std::atomic<long> atomic_counter{0};

    // --- Mutex benchmark ---
    auto start_mutex = std::chrono::high_resolution_clock::now();
    for (long i = 0; i < iterations; ++i) {
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
    }
    auto end_mutex = std::chrono::high_resolution_clock::now();

    double mutex_sec = std::chrono::duration<double>(end_mutex - start_mutex).count();
    double mutex_ns = (mutex_sec * 1e9) / iterations;

    std::cout << "Mutex benchmark:\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(6) << mutex_sec << " s\n";
    std::cout << "Average per lock/unlock: " << std::setprecision(2) << mutex_ns << " ns\n\n";

    // --- Atomic benchmark ---
    auto start_atomic = std::chrono::high_resolution_clock::now();
    for (long i = 0; i < iterations; ++i) {
        atomic_counter.fetch_add(1, std::memory_order_seq_cst);
    }
    auto end_atomic = std::chrono::high_resolution_clock::now();

    double atomic_sec = std::chrono::duration<double>(end_atomic - start_atomic).count();
    double atomic_ns = (atomic_sec * 1e9) / iterations;

    std::cout << "Atomic benchmark:\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(6) << atomic_sec << " s\n";
    std::cout << "Average per atomic increment: " << std::setprecision(2) << atomic_ns << " ns\n";

    pthread_mutex_destroy(&mutex);
    return 0;
}

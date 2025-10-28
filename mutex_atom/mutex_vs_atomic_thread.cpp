// g++ -O2 -pthread mutex_vs_atomic_thread.cpp -o mutex_vs_atomic_thread

#include <pthread.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>

long iterations_per_thread = 1'000'000;
int num_threads = 2;

pthread_mutex_t mutex;
std::atomic<long> atomic_counter{0};

// Thread function for mutex
void* mutex_worker(void* arg) {
    for (long i = 0; i < iterations_per_thread; ++i) {
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
    }
    return nullptr;
}

// Thread function for atomic (seq_cst)
void* atomic_worker(void* arg) {
    for (long i = 0; i < iterations_per_thread; ++i) {
        atomic_counter.fetch_add(1, std::memory_order_seq_cst);
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc > 1) iterations_per_thread = std::stol(argv[1]);

    pthread_mutex_init(&mutex, nullptr);
    std::vector<pthread_t> threads(num_threads);

    // --- Mutex benchmark ---
    auto start_mutex = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_threads; ++i)
        pthread_create(&threads[i], nullptr, mutex_worker, nullptr);
    for (int i = 0; i < num_threads; ++i)
        pthread_join(threads[i], nullptr);
    auto end_mutex = std::chrono::high_resolution_clock::now();

    double mutex_sec = std::chrono::duration<double>(end_mutex - start_mutex).count();
    double total_mutex_ops = iterations_per_thread * num_threads;
    std::cout << "Mutex benchmark:\n";
    std::cout << "Threads: " << num_threads << ", Iterations/thread: " << iterations_per_thread << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(6) << mutex_sec << " s\n";
    std::cout << "Average per lock/unlock: " << std::setprecision(2) << (mutex_sec*1e9/total_mutex_ops) << " ns\n\n";

    // --- Atomic benchmark (seq_cst) ---
    atomic_counter.store(0, std::memory_order_seq_cst);
    auto start_atomic = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_threads; ++i)
        pthread_create(&threads[i], nullptr, atomic_worker, nullptr);
    for (int i = 0; i < num_threads; ++i)
        pthread_join(threads[i], nullptr);
    auto end_atomic = std::chrono::high_resolution_clock::now();

    double atomic_sec = std::chrono::duration<double>(end_atomic - start_atomic).count();
    std::cout << "Atomic benchmark (seq_cst):\n";
    std::cout << "Threads: " << num_threads << ", Iterations/thread: " << iterations_per_thread << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(6) << atomic_sec << " s\n";
    std::cout << "Average per atomic increment: " << std::setprecision(2) << (atomic_sec*1e9/total_mutex_ops) << " ns\n";

    pthread_mutex_destroy(&mutex);
    return 0;
}

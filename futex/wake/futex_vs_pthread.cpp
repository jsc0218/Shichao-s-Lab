#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>

static int futex_wait(volatile int* addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, nullptr, nullptr, 0);
}

static int futex_wake(volatile int* addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, nullptr, nullptr, 0);
}

int main(int argc, char** argv) {
    int nthreads = (argc > 1) ? atoi(argv[1]) : 8;
    int ntrials = (argc > 2) ? atoi(argv[2]) : 1000;

    std::cout << "Threads: " << nthreads << ", Trials: " << ntrials << "\n";

    //------------------------------------------------------------------
    // FUTEX TEST
    //------------------------------------------------------------------
    alignas(4) int futex_val = 0; // futex word must be 4-byte aligned
    std::atomic<int> ready(0);

    auto futex_test = [&]() {
        std::vector<double> latencies;
        latencies.reserve(ntrials);
        for (int i = 0; i < ntrials; i++) {
            ready = 0;
            futex_val = 0;
            std::vector<std::thread> threads;
            threads.reserve(nthreads);

            for (int t = 0; t < nthreads; t++) {
                threads.emplace_back([&]() {
                    ready.fetch_add(1, std::memory_order_relaxed);
                    // Wait until futex_val changes
                    while (true) {
                        int expected = futex_val;
                        if (expected != 0)
                            break;
                        futex_wait(&futex_val, 0);
                    }
                });
            }

            // Wait until all threads are waiting
            while (ready.load() < nthreads)
                std::this_thread::yield();

            auto start = std::chrono::high_resolution_clock::now();

            futex_val = 1;
            futex_wake(&futex_val, nthreads);

            for (auto& th : threads)
                th.join();

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> dur = end - start;
            latencies.push_back(dur.count());
        }

        double avg = 0;
        for (auto v : latencies)
            avg += v;
        avg /= latencies.size();
        std::cout << "[FUTEX] Average wake time: " << avg / nthreads
                  << " us/thread (total " << avg << " us)\n";
    };

    //------------------------------------------------------------------
    // PTHREAD MUTEX + COND TEST
    //------------------------------------------------------------------
    auto pthread_test = [&]() {
        std::mutex m;
        std::condition_variable cv;
        bool ready_flag = false;
        std::atomic<int> wait_ready(0);
        std::vector<double> latencies;
        latencies.reserve(ntrials);

        for (int i = 0; i < ntrials; i++) {
            ready_flag = false;
            wait_ready = 0;

            std::vector<std::thread> threads;
            threads.reserve(nthreads);
            for (int t = 0; t < nthreads; t++) {
                threads.emplace_back([&]() {
                    std::unique_lock<std::mutex> lk(m);
                    wait_ready.fetch_add(1, std::memory_order_relaxed);
                    cv.wait(lk, [&] { return ready_flag; });
                });
            }

            while (wait_ready.load() < nthreads)
                std::this_thread::yield();

            auto start = std::chrono::high_resolution_clock::now();
            {
                std::lock_guard<std::mutex> lk(m);
                ready_flag = true;
                cv.notify_all();
            }
            for (auto& th : threads)
                th.join();
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> dur = end - start;
            latencies.push_back(dur.count());
        }

        double avg = 0;
        for (auto v : latencies)
            avg += v;
        avg /= latencies.size();
        std::cout << "[PTHREAD] Average wake time: " << avg / nthreads
                  << " us/thread (total " << avg << " us)\n";
    };

    //------------------------------------------------------------------
    futex_test();
    pthread_test();

    return 0;
}

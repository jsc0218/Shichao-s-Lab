// Shared queue + mutex: more threads => more lock contention => slower.
//
// Build & run:
//   g++ -std=c++17 -O2 -pthread -o queue_contention queue_contention.cpp
//   ./queue_contention

#include <chrono>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace std;

constexpr size_t N = 5'000'000;

double bench(int threads) {
    queue<int> q;
    mutex mu;
    const size_t each = N / threads;
    auto t0 = chrono::steady_clock::now();

    auto push_all = [&](size_t count) {
        for (size_t i = 0; i < count; ++i) {
            lock_guard<mutex> lock(mu);
            q.push((int)i);
        }
    };

    if (threads == 1) {
        push_all(N);
    } else {
        vector<thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([&] { push_all(each); });
        for (auto& w : workers)
            w.join();
    }

    if (q.size() != N) {
        cerr << "error: expected " << N << ", got " << q.size() << '\n';
        exit(1);
    }
    return chrono::duration<double>(chrono::steady_clock::now() - t0).count();
}

int main() {
    double baseline = 0;
    cout << "threads  seconds  slowdown\n";
    for (int n : {1, 2, 4, 8, 16}) {
        double sec = bench(n);
        if (n == 1)
            baseline = sec;
        cout << n << "  " << sec << "  " << sec / baseline << "x\n";
    }
}

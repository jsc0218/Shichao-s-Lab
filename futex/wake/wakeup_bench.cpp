// Benchmark: serial vs dedicated-thread vs parallel-pool commit-waiter wakeup.
//
// N backends block on per-backend futex. One wal-writer wakes them via:
//   serial    - walwriter calls futex_wake for every backend (syncrep style)
//   dedicated - walwriter hands off to one background thread (serial wakes there)
//   parallel  - walwriter batches to a thread pool (wal_write.c style, batch=10)
//
// Build: make wakeup_bench
// Run:   ./wakeup_bench [--backends 64] [--rounds 100]


#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <linux/futex.h>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace std::chrono;

namespace {

constexpr int kBatchSize = 10;

enum class WakeMode { Serial, Dedicated, Parallel };

struct Futex {
    atomic<uint32_t> value{0};
};

inline void futex_reset(Futex* f) {
    f->value.store(0, memory_order_relaxed);
}

inline void futex_wait(Futex* f) {
    while (f->value.load(memory_order_acquire) == 0) {
        syscall(SYS_futex, &f->value, FUTEX_WAIT, 0, nullptr, nullptr, 0);
    }
}

inline void futex_wake(Futex* f) {
    f->value.store(1, memory_order_release);
    syscall(SYS_futex, &f->value, FUTEX_WAKE, 1, nullptr, nullptr, 0);
}

void wake_serial(vector<Futex>& futexes) {
    for (auto& f : futexes) {
        futex_wake(&f);
    }
}

// One background thread; walwriter posts work and returns immediately.
class DedicatedWaker {
public:
    DedicatedWaker() : worker_([this] { loop(); }) {}

    ~DedicatedWaker() {
        {
            lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_one();
        worker_.join();
    }

    void submit(vector<Futex>* futexes) {
        pending_.fetch_add(1, memory_order_acq_rel);
        {
            lock_guard lock(mutex_);
            futexes_ = futexes;
        }
        cv_.notify_one();
    }

    void drain() {
        while (pending_.load(memory_order_acquire) != 0) {
            this_thread::yield();
        }
    }

private:
    void loop() {
        while (true) {
            vector<Futex>* work = nullptr;
            {
                unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || futexes_ != nullptr; });
                if (stop_) {
                    return;
                }
                work = futexes_;
                futexes_ = nullptr;
            }
            wake_serial(*work);
            pending_.fetch_sub(1, memory_order_acq_rel);
        }
    }

    thread worker_;
    mutex mutex_;
    condition_variable cv_;
    vector<Futex>* futexes_ = nullptr;
    atomic<int> pending_{0};
    bool stop_ = false;
};

class ThreadPool {
public:
    explicit ThreadPool(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock lock(mutex_);
                        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                    pending_.fetch_sub(1, memory_order_acq_rel);
                }
            });
        }
    }

    ~ThreadPool() {
        {
            lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }

    void submit(function<void()> task) {
        pending_.fetch_add(1, memory_order_acq_rel);
        {
            lock_guard lock(mutex_);
            tasks_.push(move(task));
        }
        cv_.notify_one();
    }

    void drain() {
        while (pending_.load(memory_order_acquire) != 0) {
            this_thread::yield();
        }
    }

private:
    vector<thread> workers_;
    queue<function<void()>> tasks_;
    mutex mutex_;
    condition_variable cv_;
    atomic<int> pending_{0};
    bool stop_ = false;
};

struct Config {
    int backends = 64;
    int rounds = 100;
    int warmup = 10;
};

struct Stats {
    vector<double> dispatch_us;
    vector<double> complete_us;
    vector<double> backend_us;
};

double now_us() {
    return duration<double, micro>(steady_clock::now().time_since_epoch()).count();
}

double avg(const vector<double>& v) {
    return v.empty() ? 0.0 : accumulate(v.begin(), v.end(), 0.0) / v.size();
}

Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if (arg == "--help") {
            cout << "usage: wakeup_bench [--backends N] [--rounds N] [--warmup N]\n";
            exit(0);
        }
        if (i + 1 >= argc) {
            cerr << "missing value for " << arg << '\n';
            exit(2);
        }
        if (arg == "--backends") {
            cfg.backends = stoi(argv[++i]);
        } else if (arg == "--rounds") {
            cfg.rounds = stoi(argv[++i]);
        } else if (arg == "--warmup") {
            cfg.warmup = stoi(argv[++i]);
        } else {
            cerr << "unknown arg: " << arg << '\n';
            exit(2);
        }
    }
    return cfg;
}

long extra_threads(WakeMode mode) {
    switch (mode) {
        case WakeMode::Dedicated:
            return 1;
        case WakeMode::Parallel:
            return max(sysconf(_SC_NPROCESSORS_ONLN) / 8, 1L);
        default:
            return 0;
    }
}

void warn_thread_limit(int backends, WakeMode mode) {
    rlimit lim {};
    if (getrlimit(RLIMIT_NPROC, &lim) != 0 || lim.rlim_cur == RLIM_INFINITY) {
        return;
    }
    const long need = backends + extra_threads(mode) + 1;
    if (static_cast<long>(lim.rlim_cur) < need) {
        cerr << "warning: need ~" << need << " threads, ulimit -u is "
             << lim.rlim_cur << ". try --backends "
             << max(1L, static_cast<long>(lim.rlim_cur) / 4) << '\n';
    }
}

void wake_parallel(ThreadPool& pool, vector<Futex>& futexes) {
    for (size_t i = 0; i < futexes.size(); i += kBatchSize) {
        const size_t end = min(i + kBatchSize, futexes.size());
        vector<Futex*> batch;
        batch.reserve(end - i);
        for (size_t j = i; j < end; ++j) {
            batch.push_back(&futexes[j]);
        }
        pool.submit([batch = move(batch)] {
            for (Futex* f : batch) {
                futex_wake(f);
            }
        });
    }
}

Stats run_mode(const Config& cfg, WakeMode mode) {
    warn_thread_limit(cfg.backends, mode);

    const int total_rounds = cfg.warmup + cfg.rounds;
    vector<Futex> futexes(static_cast<size_t>(cfg.backends));
    atomic<int> ready{0};
    atomic<int> done{0};
    mutex stats_mu;
    Stats stats;

    optional<DedicatedWaker> dedicated;
    optional<ThreadPool> pool;
    if (mode == WakeMode::Dedicated) {
        dedicated.emplace();
    } else if (mode == WakeMode::Parallel) {
        pool.emplace(extra_threads(WakeMode::Parallel));
    }

    vector<thread> backends;
    backends.reserve(cfg.backends);
    for (int i = 0; i < cfg.backends; ++i) {
        try {
            backends.emplace_back([&, idx = i] {
                Futex& f = futexes[idx];
                for (int round = 0; round < total_rounds; ++round) {
                    futex_reset(&f);
                    ready.fetch_add(1, memory_order_acq_rel);

                    const double t0 = now_us();
                    futex_wait(&f);
                    const double wake_us = now_us() - t0;

                    if (round >= cfg.warmup) {
                        lock_guard lock(stats_mu);
                        stats.backend_us.push_back(wake_us);
                    }
                    done.fetch_add(1, memory_order_acq_rel);
                }
            });
        } catch (const system_error& e) {
            cerr << "thread create failed at " << i << '/' << cfg.backends
                 << ": " << e.what()
                 << "\ntry: ./wakeup_bench --backends 64  (check ulimit -u)\n";
            exit(1);
        }
    }

    for (int round = 0; round < total_rounds; ++round) {
        while (ready.load(memory_order_acquire) < cfg.backends) {
            this_thread::yield();
        }
        ready.store(0, memory_order_release);

        const double t0 = now_us();
        switch (mode) {
            case WakeMode::Serial:
                wake_serial(futexes);
                break;
            case WakeMode::Dedicated:
                dedicated->submit(&futexes);
                break;
            case WakeMode::Parallel:
                wake_parallel(*pool, futexes);
                break;
        }
        const double dispatch_us = now_us() - t0;

        if (mode == WakeMode::Dedicated) {
            dedicated->drain();
        } else if (mode == WakeMode::Parallel) {
            pool->drain();
        }
        const double complete_us = now_us() - t0;

        while (done.load(memory_order_acquire) < cfg.backends) {
            this_thread::yield();
        }
        done.store(0, memory_order_release);

        if (round >= cfg.warmup) {
            stats.dispatch_us.push_back(dispatch_us);
            stats.complete_us.push_back(complete_us);
        }
    }

    for (auto& t : backends) {
        t.join();
    }
    return stats;
}

void print_summary(const char* label, const Stats& s) {
    cout << left << setw(12) << label
         << " dispatch " << fixed << setprecision(1) << avg(s.dispatch_us)
         << " us | complete " << avg(s.complete_us) << " us"
         << " | backend avg " << avg(s.backend_us) << " us\n";
}

void print_speedup(const char* label, const Stats& base, const Stats& other) {
    cout << "  " << left << setw(12) << label
         << " dispatch " << setprecision(2) << avg(base.dispatch_us) / avg(other.dispatch_us)
         << "x | end-to-end " << avg(base.complete_us) / avg(other.complete_us) << "x\n";
}

}  // namespace

int main(int argc, char** argv) {
    const Config cfg = parse_args(argc, argv);
    const long pool_workers = extra_threads(WakeMode::Parallel);

    cout << "Wakeup benchmark (futex, batch=" << kBatchSize << ")\n"
         << "backends=" << cfg.backends << " rounds=" << cfg.rounds
         << " warmup=" << cfg.warmup
         << " hw_threads=" << thread::hardware_concurrency()
         << " pool_workers=" << pool_workers << "\n\n";

    cout << "serial (walwriter wakes every backend inline)\n";
    const Stats serial = run_mode(cfg, WakeMode::Serial);
    print_summary("serial", serial);

    cout << "dedicated (one background thread wakes every backend)\n";
    const Stats dedicated = run_mode(cfg, WakeMode::Dedicated);
    print_summary("dedicated", dedicated);

    cout << "parallel (thread pool batches of " << kBatchSize << ")\n";
    const Stats parallel = run_mode(cfg, WakeMode::Parallel);
    print_summary("parallel", parallel);

    cout << "\nvs serial:\n";
    print_speedup("dedicated", serial, dedicated);
    print_speedup("parallel", serial, parallel);

    return 0;
}

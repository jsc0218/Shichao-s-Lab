## Mutex vs Atomic Benchmark

![Benchmark Results](mutext_vs_atomic.png)


Single thread case:
Mutex Average per lock/unlock: 6.93 ns
Atomic Average per atomic increment: 2.42 ns

2 threads case:
Mutex Average per lock/unlock: 27.31 ns
Atomic Average per atomic increment: 5.86 ns

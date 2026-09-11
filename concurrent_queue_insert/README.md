## Concurrent Queue Insert Benchmark

Compare single-threaded vs multi-threaded inserts into a shared `std::queue` protected by a `std::mutex`.

More threads contending on one lock => worse throughput.

### Build & Run

```bash
g++ -std=c++17 -O2 -pthread -o queue_contention queue_contention.cpp
./queue_contention
```

### Example output

```
threads  seconds  slowdown
1  0.279  1x
2  2.298  8.22x
4  3.272  11.71x
8  6.707  24.01x
16  7.578  27.12x
```

`slowdown` = time ratio vs 1 thread (>1x means slower).

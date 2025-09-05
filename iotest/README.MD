## Latency vs Batch Size in Socket Writing

![Benchmark Results](io.png)



| Case | Description                              | Time (s)       | Time (ms)    |
|------|------------------------------------------|----------------|--------------|
| 1    | Unbuffered async                         | 14.4735 s      | 14473.527 ms |
| 1a   | Async buffered, flush each               | 15.9191 s      | 15919.082 ms |
| 1b   | Async buffered, flush once               | 0.0405 s       | 40.476 ms    |
| 2    | Unbuffered std::fs                       | 2.3050 s       | 2304.986 ms  |
| 2a   | std::fs + BufWriter, flush each          | 2.3282 s       | 2328.247 ms  |
| 2b   | std::fs + BufWriter, flush once          | 0.0168 s       | 16.756 ms    |
| 3    | Single bulk write                        | 0.0152 s       | 15.188 ms    |

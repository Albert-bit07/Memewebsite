# Exact retrieval benchmark

This experiment compares the original full-sort reference with the production partial-sort top-k path. Both score all unseen candidates using the same cosine formula, exclude every tenth item, and return 20 results. Every query checks exact ordered ID equality before accepting a timing result.

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DBUILD_SERVER=OFF -DBUILD_BENCHMARKS=ON
cmake --build build-bench -j2
build-bench/backend/cpp/retrieval_benchmark /tmp/samples.csv > /tmp/results.csv
```

Synthetic catalogs: 1,000, 10,000 and 100,000 uniformly generated 64-dimensional vectors; seed 20261020. Each size uses one discarded warm-up and 20 measured queries, alternating execution order. Data generation, file loading, equality checks and sample-file writes are outside measured regions. Timings include candidate scoring, selection and result construction. Median averages the two central samples; p95 uses nearest-rank sample 19 of 20. QPS is measured query count divided by summed timed duration, not concurrent server throughput.

## Recorded run — 2026-10-20

Linux x86_64, AMD EPYC 9V74 virtualized host exposing 9 CPUs, GCC 13.3.0, CMake Release. No CPU pinning or exclusive host allocation. See [all 120 timed samples](samples-20261020.csv) and [aggregated results](results-20261020.csv).

| Items | Full sort median | Partial sort median | Full sort p95 | Partial sort p95 |
|---:|---:|---:|---:|---:|
| 1,000 | 0.098252 ms | 0.066259 ms | 0.138628 ms | 0.129655 ms |
| 10,000 | 1.15547 ms | 0.645639 ms | 1.25602 ms | 0.865368 ms |
| 100,000 | 13.9733 ms | 7.21584 ms | 16.6797 ms | 9.32101 ms |

Both methods matched for all 63 queries, including warm-ups. Process peak RSS reached 49,532 KiB at the largest size. RSS is the whole process high-water mark across both methods and catalog preparation, not a per-method allocation measurement. Both methods retain O(N) scores; this change does not claim bounded O(K) memory.

The result supports using partial sorting for small K in this exact-retrieval path. It does not establish real-image relevance, general latency guarantees, HTTP overhead, concurrent throughput, normalized-vector gains or approximate-search recall. The committed real catalog is much smaller and has different dimensions. Larger load experiments, separate preference/scoring timings, precomputed normalization, and user relevance evaluation remain future work. CI checks exact agreement but intentionally imposes no timing threshold.

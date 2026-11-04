# Optional variety and exploration

Choose **Variety + exploration** in the feed, or request
`GET /recommend?strategy=balanced` with an existing session cookie. The selector
resets to similarity on page reload. Omitting `strategy` or using `exact` retains
the original ranking. Unknown, empty or repeated strategy parameters return 400.
No likes still uses the existing session discovery ordering in either mode.

For personalized requests, the native service:

1. Retrieves the best 100 eligible candidates by exact cosine similarity.
2. Greedily selects up to 19 candidates using
   `0.75 * preference_similarity - 0.25 * redundancy`, where redundancy is the
   largest cosine similarity to an already selected item, floored at zero.
3. Adds one other eligible item using the smallest FNV-1a hash of the session
   token, a zero-byte separator and stable item ID across the whole catalog.

At most 20 items are returned. Seen/liked items and duplicates are excluded.
With one eligible item only that item is returned; with none the list is empty.
Score ties retain candidate order (similarity, then catalog index); hash ties
retain catalog order. Identical catalog, preference, exclusions and session give
identical results across restart. A catalog reorder or new feedback may change
results. The exploration ordering is deterministic, not uniform randomness,
a bandit, or a claim of relevance. Refresh does not rotate unseen exploration
items unless the inputs change. With a zero preference the variety rule still
applies; exact mode retains catalog ordering for ties.

The coefficients and pool size are explicit heuristics, not tuned against human
judgments. No recommendation quality improvement is established. Use the
[held-out evaluation protocol](relevance-evaluation.md) with separately captured
exact and balanced outputs before making quality claims. Captions explain the
selection rule, not confidence or why a person will like an image.

## Cost and reproduction

`retrieval_benchmark` now compares full sort, exact partial sort and balanced
ranking on the same synthetic vectors/queries. It validates exact equivalence
between the first two, and uniqueness/exclusions for balanced results. The latter
intentionally differs from exact ranking. One warmup precedes 20 measured queries;
method order rotates each trial. The workload uses seed 20261020, 64 dimensions,
k=20 and excludes every tenth item. Hashing the catalog for discovery adds an
O(N) scan; reranking is bounded by 100 candidates and 19 selections, but overall
retrieval still scans the whole catalog. No Python/model runs in this path.

One local Linux run on 2026-11-04, g++ 13.3.0, C++17, `-O2`:

| Catalog items | Exact median ms | Balanced median ms | Exact p95 ms | Balanced p95 ms |
|---|---:|---:|---:|---:|
| 1,000 | 0.066725 | 0.310191 | 0.123786 | 0.519350 |
| 10,000 | 0.685129 | 1.588770 | 0.870187 | 2.092930 |
| 100,000 | 7.151100 | 14.669900 | 7.818640 | 15.335300 |

[Raw measured samples](../benchmarks/balanced-samples-20261104.csv) accompany this
run. These shared-host observations show extra cost, not a speedup or latency
SLO. They exclude HTTP, storage, concurrent load, image rendering and inference;
synthetic vectors establish no useful recommendation quality. Process-wide peak
RSS includes all methods and cannot attribute memory to one policy.

```sh
g++ -std=c++17 -O2 -Ibackend/cpp benchmarks/retrieval.cpp \
  backend/cpp/recommender.cpp backend/cpp/utils.cpp -o /tmp/retrieval-benchmark
/tmp/retrieval-benchmark /tmp/retrieval-samples.csv
```

Native tests cover deterministic reranking, small/empty/excluded candidate sets,
request bounds and exact-mode preservation. HTTP tests exercise authenticated
strategy selection, invalid parameters, exclusions and process restart. Browser
tests exercise selecting both policies and their captions with the native server.
These controlled tests are separate from relevance evaluation or formal proof.

# Offline relevance evaluation

`benchmarks/relevance.py` evaluates supplied rankings against frozen binary
held-out positives, with deterministic random and training-popularity baselines.
It does not call a model or serving API, generate labels, or establish usefulness.

## Freeze the experiment

1. Use permitted images and consenting participants. Record collection provenance,
   catalog/model versions, cohort criteria and an interaction cutoff in private
   experiment notes. Use pseudonymous query IDs, never session tokens.
2. Build preferences only from pre-cutoff likes. Put all items seen or liked before
   the cutoff in `excluded_ids`. Collect held-out judgments separately; they must
   not enter preference calculation or training-popularity counts.
3. Freeze the catalog and compute popularity only from training interactions.
   Missing counts mean zero. Validation checks membership/exclusion, but cannot
   establish consent, chronology or truthful provenance.
4. Capture stable IDs in ranked order from each policy using exactly those
   preferences, exclusions and catalog snapshot. `/recommend` supplies IDs and
   `catalog_version`; exporting evaluation queries is an operator step, not
   automated by this script.
5. Compare policies using identical dataset bytes, k and seed. Tune on a separate
   development split; do not repeatedly tune on the final holdout.

Every query must have at least one held-out positive. Results describe that
selected cohort, not all users or cold-start utility. Unlisted items count as
nonrelevant; incomplete judgments can bias scores. Report the labeling method,
cohort size and missing-output count alongside results.

## Inputs

This example is **synthetic**, solely to illustrate the schema:

```json
{
  "schema_version": 1,
  "dataset_id": "synthetic-example-v1",
  "catalog_version": "fixture-v1",
  "catalog_ids": ["a", "b", "c", "d"],
  "training_popularity": {"a": 10, "b": 2, "c": 1},
  "queries": [
    {"id": "query-1", "excluded_ids": ["a"], "relevant_ids": ["b"]}
  ]
}
```

A separate file contains one policy's predictions:

```json
{
  "schema_version": 1,
  "dataset_id": "synthetic-example-v1",
  "catalog_version": "fixture-v1",
  "rankings": {"query-1": ["c", "b"]}
}
```

```bash
python3 benchmarks/relevance.py dataset.json predictions.json --k 2 --seed experiment-1 > report.json
```

Dataset/catalog identities must match. Catalog, query and per-query item IDs must
be unique nonempty strings of at most 256 characters. Duplicate rankings, unknown
IDs, excluded recommendations, overlapping exclusions/positives, unknown query
outputs and duplicate JSON keys are rejected. All supplied ranking positions are
checked, including those below k. Missing query outputs are empty rankings and
remain in every denominator.

Inputs are limited to 10 MiB each and one million catalog-item × query combinations;
k ranges from 1 to 100. Invalid input produces no partial report or echoed input.
Reports include hashes of the exact input files. Preserve the inputs and evaluator
commit with each experiment; hashes alone do not preserve data.

## Metrics and baselines

Each metric is macro-averaged over **all dataset queries**:

| Metric | Per-query definition |
|---|---|
| Precision@k | Relevant results in the first k positions divided by k. Short lists still divide by k, even when fewer candidates exist. |
| Recall@k | Retrieved held-out positives divided by all held-out positives. |
| nDCG@k | Binary gain discounted by `1/log2(rank+1)` for one-based ranks, divided by the ideal ordering of up to k positives. |
| MRR@k | Reciprocal rank of the first relevant result within k, or zero. |

Missing outputs score zero on every metric. Failed queries are not dropped and
duplicate results cannot earn extra credit. `missing_rankings` counts absent
query keys, separately from explicitly empty lists.

The random baseline orders eligible IDs by SHA-256 of JSON `[seed, query_id,
item_id]`, then ID. It is reproducible across processes/input order. Compare
multiple preselected seeds before drawing conclusions from random-baseline
variation. Popularity sorts training counts descending, then IDs ascending.
Neither baseline reads submitted rankings or holdout positives to build its order.

These metrics measure agreement with supplied judgments, not latency, approximate
retrieval recall, diversity, satisfaction or causal online lift. The synthetic
retrieval benchmark answers a different question. Tests validate arithmetic and
data boundaries with synthetic fixtures; this implementation claims no real
collection evaluation, live-model quality or recommendation improvement.

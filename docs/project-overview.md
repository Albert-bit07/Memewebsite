# Serving boundary

The server is C++17 only. `preference.hpp` maintains a sum and count; normalization of the sum is equivalent to normalization of the mean. Last-like removal clears floating-point residue. Membership checks in HTTP feedback handling make duplicate like/unlike requests idempotent.

Catalog vectors are read-only after startup. Recommendation handlers copy preference state under the mutex and rank after releasing it. `load_catalog` validates a candidate object completely before moving it into place; a failed load does not partially mutate the current object. There is no concurrent in-place reload API.

Offline generation writes a new directory with a schema-versioned manifest, stable filename-derived IDs, model metadata, vector dimensions and image files. The checked-in artifact marks legacy model provenance unknown. Schema and model versions are different concepts; compatible shapes do not establish that two models share a vector space.

Full sorting remains the benchmark reference; production warm ranking selects top-k with partial sorting after seen-item exclusion. The committed synthetic harness compares exact results and combined scoring/selection timings. Preference calculation, scoring-only, concurrent requests and HTTP overhead still need separate measurements. Synthetic performance does not establish relevance on real images. SQLite now persists independent anonymous sessions, item feedback and event history. Changes commit before derived preference sums are updated; restart reconstruction uses stable IDs. The process-local cache is bounded but is not coherent across multiple server processes. Seen items are filtered, and cold-start order varies by session.

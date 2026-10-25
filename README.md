# Meme Feed — native recommendation serving

**A C++ recommendation server with an offline Python embedding pipeline and a browser meme-feed demo.** Image vectors are prepared once; recommendation requests do not start Python, load CLIP or cross a language boundary.

The current retrieval baseline is deliberately straightforward: maintain a vector sum for liked images, normalize it, scan the immutable catalog with cosine similarity, and select the highest scores. This makes storage, concurrency and retrieval improvements independently testable.

## Architecture

```mermaid
flowchart TD
  I[Image collection] --> P[Offline Python and CLIP]
  P --> A[(Versioned catalog artifact)]
  A --> V[C++ catalog validation]
  V --> R[Read-only vectors and item IDs]
  B[Browser] --> H[C++ HTTP routes]
  H --> U[Mutable preference sum]
  U --> S[Cosine scan and ranking]
  R --> S
  S --> B
```

Python generates vectors, images and a manifest into a new artifact directory. C++ validates metadata, dimensions, item IDs, file paths and vector values before replacing a loaded catalog. A failed load leaves the previous catalog intact. The server loads its catalog at startup; live HTTP catalog replacement is not yet exposed.

## Build and run

Serving requires a C++17 compiler, CMake, SQLite development headers/library and threads. On Ubuntu, install `libsqlite3-dev`. Python is only needed for the test driver or offline generation, not the server executable.

```bash
cmake -S . -B build-native
cmake --build build-native -j2
ctest --test-dir build-native --output-on-failure
python tests/server_smoke.py build-native/backend/cpp/recommender
python -m unittest discover -s tests -p 'test_*.py' -v
cd backend
../build-native/backend/cpp/recommender
```

Open `http://127.0.0.1:18080` in a browser; the C++ server serves the frontend from `../frontend` when launched from `backend/`. Separate browser profiles receive independent anonymous sessions. You can pass an alternate manifest path as the server's first argument, a SQLite database path as the second, and a frontend directory as the third. Paths inside the manifest resolve relative to the manifest directory, not the process working directory.

## Catalog contract

`backend/data/catalog.json` describes the checked-in vectors. It intentionally records their model provenance as `unknown-legacy-provenance`: the repository contains a CLIP generator, but that does not prove exactly how the committed vectors were produced.

A schema-v1 manifest contains `catalog_version`, `model_version`, `dimension`, a relative CSV `vectors` path, and ordered `items` with unique stable IDs and relative image paths. Item order corresponds to vector rows. The generator derives IDs from the relative image filename; preserving filenames preserves identity across generations, while renaming changes identity. IDs are not vector indexes. Artifact paths are resolved before containment validation: symlinks within the artifact are supported, but links outside it are rejected. Keep the artifact directory read-only while serving; validation does not protect against a local process changing files afterward.

For a new image collection, install the offline pipeline's PyTorch, OpenAI CLIP, Pillow and NumPy dependencies in a separate environment, then run:

```bash
python backend/python/embed_memes.py /tmp/meme-catalog-v2 --version v2 --images /path/to/permitted/images
build-native/backend/cpp/recommender /tmp/meme-catalog-v2/catalog.json
```

The output directory must not exist. Files are assembled in a temporary sibling directory and renamed only after successful generation. An error removes that temporary artifact. CLIP regeneration has not been executed in the current validation; the artifact writer is tested with synthetic vectors.

## Preference and concurrency behavior

Likes and unlikes update a C++ vector sum incrementally. Duplicate likes and unlikes are idempotent at the HTTP boundary; removing the last like resets the sum exactly. A recommendation request copies the normalized preference under a short mutex, then releases it before scoring immutable vectors. Concurrent requests therefore do not hold the user-state lock through the full ranking scan.

Warm ranking uses partial sorting for O(ND + N log K) work and O(N) score storage. [The reproducible synthetic benchmark](benchmarks/README.md) compares this path with full sorting and checks exact output equality. These in-process measurements do not establish HTTP throughput or recommendation quality. Cold-start preference vectors are zero; session-specific ordering supplies initial variety, and seen items are filtered.

## Persistent personalization

The browser obtains an opaque session cookie from `POST /session` (HttpOnly, SameSite=Strict). SQLite stores sessions, per-item liked/seen state and a transition event history. State changes and their events commit together before in-memory preferences change. Duplicate feedback does not add duplicate transitions. On restart, liked stable item IDs reconstruct each session's vector sum against the current catalog.

`GET /recommend` excludes items marked seen. The browser reports actual visibility through one shared intersection observer. New sessions use a deterministic session-specific cold-start order; this is exploration, not a claim of learned relevance. Removing the last like resets the preference exactly. Ranking happens after releasing the storage/state mutex. The derived-state cache is bounded at 1024 sessions; eviction leaves durable interactions intact.

## Current boundaries and next milestones

This remains a single-instance loopback demo. Anonymous cookies are not authenticated accounts; losing the cookie loses access to that session. There is no account recovery, session expiration cleanup or rate limiting. Do not run multiple server instances against the same database: their derived in-memory caches do not coordinate. Dataset permissions, model provenance, browser E2E, vendored-header review, live catalog replacement, concurrent-load measurements and relevance evaluation remain outstanding.

CI builds the full native server, runs catalog/preference regressions, and tests independent HTTP sessions, duplicate feedback, concurrent ranking, seen filtering and preference reconstruction across two server restarts. Catalog tests reject invalid schema versions, dimensions, duplicate IDs, path traversal and symlink escapes while preserving the prior loaded version. There is no production deployment or formal verification claim.

**Ownership:** collaborative repository; Albert authored the original [CLIP integration](https://github.com/Albert-bit07/Memewebsite/commit/bb6ad40a65eae81cee159e1aaf0b4f2e9fee0568). Subsequent engineering work is visible in commit and PR history.

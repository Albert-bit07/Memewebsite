> Historical review: this page describes the pre-repair baseline. For current build commands, fixes and remaining limitations, use the [README](../README.md).

# Engineering overview

## Main decisions

The [C++ recommender](../backend/cpp/recommender.cpp) does an exhaustive scan and full sort. At the checked 54-image scale, a vector index would add infrastructure without demonstrated need. No runtime benchmark or retrieval-quality experiment establishes this implementation's performance or usefulness.

[Embedded Python](../backend/cpp/main.cpp) reuses NumPy preference computation and CLIP initialization but crosses language/runtime boundaries. Each recommendation copies embeddings into Python-compatible structures; a C++ lock serializes recommendation work but does not manage the GIL. Defining interpreter lifetime and worker-thread ownership is the most consequential next systems task.

[CSV parsing](../backend/cpp/utils.cpp) replaces malformed numeric cells with zero and cosine similarity compares only the minimum dimension. Those choices can hide corrupt inputs. Missing path/vector correspondence can also cause invalid indexing. The checked artifacts have equal row counts and dimensions, but the implementation does not establish general dataset integrity.

## Model and data scope

CLIP ViT-B/32 is a pretrained image encoder invoked for inference, with normalized embeddings and no training/fine-tuning loop here. There are no train/test splits, labeled relevance judgments, offline ranking metrics, A/B tests or adoption evidence. Stored arrays and CSV are artifacts, not a reproducible proof of their producing model/version. Regenerating paths and vectors together is necessary on a new machine.

## Failure scenarios and next work

Empty embeddings can fail the no-like preference path. Out-of-range feedback indexes can fail Python array access. Missing images return 404; malformed request JSON returns 400; recommendation exceptions become 500 if caught, while GIL/runtime failures may hang or abort instead. Likes disappear on restart and are shared across clients. Status reads and liked-count reporting need synchronization review.

Priorities: define Python GIL ownership, validate dimensions/indexes, make data paths portable, add per-user durable preferences and remove-like semantics, then test HTTP and concurrent requests. Add cold-start handling and relevance evaluation before calling ranking personalized at useful quality. These are future improvements, not changes in this PR.

## Validation — 2026-10-20

Python AST parsing: 2 files passed. JavaScript syntax: passed. Dataset structure: 54 rows x 512 values and 54 matching image basenames. CMake could not run (`cmake: command not found`); torch/clip/pybind11 are unavailable. No full build, model inference, HTTP request, rendered screenshot, benchmark or recording was verified. No AGENTS.md, automated tests or CI workflow was found in the checked tree. [Demo prerequisites](demo.md) are derived from source and remain unexecuted.

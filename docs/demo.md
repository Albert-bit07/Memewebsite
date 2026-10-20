> Historical review: this page describes the pre-repair baseline. For current build commands, fixes and remaining limitations, use the [README](../README.md).

# Local setup and 90-second recording plan

**Status:** unexecuted procedure with known blockers, not a working-demo claim. Use a disposable checkout; retain logs and stop if an instruction fails. The current GIL/threading boundary may prevent runtime requests even after build prerequisites are met.

## Prepare

Install a C++17 compiler, CMake >=3.15 and Python 3.10 including development headers/libraries. Create and activate a Python 3.10 environment. With that environment active:

```bash
python -m pip install numpy pillow torch torchvision pybind11
python -m pip install git+https://github.com/openai/CLIP.git
python backend/python/embed_memes.py
cmake -S backend/cpp -B demo-build
cmake --build demo-build --config Release
```

Dependencies are not locked; record resolved versions. CLIP can download weights. Regeneration overwrites only the checkout's vector/path artifacts; review those changes and do not commit machine-specific absolute paths. Verify CMake selected the intended Python 3.10 interpreter; its hardcoded Windows hint can require a local configuration correction. The checked CMake source was not changed by this documentation work.

Start the binary from the **backend** directory in a separate terminal:

```bash
cd backend
../demo-build/recommender
```

On a multi-configuration Windows build use the generated Release executable path instead. Serve the frontend from the repository root in another terminal:

```bash
python -m http.server 8000 --bind 127.0.0.1 --directory frontend
```

Open `http://127.0.0.1:8000`. Inspect the loopback API before recording:

```bash
curl --fail http://127.0.0.1:18080/status
curl --fail http://127.0.0.1:18080/recommend
```

If requests hang or fail, capture the error and do not present the feed as verified. Do not replace inference with synthetic success output.

## Narration

- **0–20 s:** show the architecture and describe the image-to-vector pipeline.
- **20–45 s:** if verified locally, scroll real served images and like one.
- **45–70 s:** refresh recommendations and inspect returned indexes. Describe a ranking recomputation, without promising an observable relevance gain for one like.
- **70–95 s:** show cosine ranking and the global preference list; explain cold start, no per-user persistence and the Python threading risk.

Stop only the processes you started with Ctrl-C. Delete the disposable checkout only after reviewing its exact location. A source walkthrough is an honest fallback if runtime blockers remain; label it accordingly.

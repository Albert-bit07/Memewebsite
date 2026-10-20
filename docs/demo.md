# Local demo

1. Build with the README commands without installing Python development libraries, NumPy or CLIP into the serving environment.
2. Start the binary from `backend/` or pass an explicit catalog path. Inspect `/status` for the catalog version and item count.
3. Open `http://127.0.0.1:18080`, like an image, inspect `/recommend`, and unlike it. The smoke test also exercises duplicate feedback and concurrent requests.
4. Run CTest: invalid catalog loads preserve the existing catalog, and removing the last like restores a zero preference.
5. Explain the boundary: Python generates artifacts offline; normal requests use C++ only. Open a second browser profile, like a different image, then restart the server using the same database. Both sessions retain independent preferences. Full sorting remains the unmeasured exact baseline.

This is a demo procedure, not a recorded browser session. Do not claim browser-verified behavior, live catalog swapping, measured performance gains or evaluated recommendation quality yet.

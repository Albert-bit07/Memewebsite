# Recruiting notes — Meme Feed

## One sentence / suggested repository description

Local C++/Python image recommendation prototype using CLIP embeddings, cosine ranking and a scrolling web feed.

## 30-second explanation

I worked on a collaborative meme-feed project and authored the CLIP recommender integration. Images become embeddings, likes form a mean preference vector through embedded Python, and C++ ranks candidates by cosine similarity behind a local HTTP API. It gave me concrete experience reasoning about language boundaries and recommendation state. The current version has shared in-memory preferences and unresolved Python thread/GIL handling, so I present it as a prototype rather than a validated serving system.

## Recruiter email paragraph

My meme-feed project connects C++ HTTP serving with Python embedding utilities and a browser interface. My authored integration commit adds CLIP-based image representation and a feedback-driven ranking path. The documentation explains the exhaustive retrieval algorithm, cold start and state limitations, with a local demonstration plan. It is a useful discussion of recommendation implementation and cross-language systems tradeoffs.

## Resume bullets

- Integrated a C++17 recommendation API with embedded Python utilities for CLIP image embeddings and normalized mean preference vectors in a collaborative web-feed project.
- Implemented cosine-similarity ranking over stored image vectors and HTTP routes for recommendations, feedback and image retrieval; documented cold-start, persistence and concurrency limitations.

Confirm the precise file-level contribution against the linked integration commit before using the second bullet; collaborative history does not establish sole ownership of every line.

## Engineering manager explanation

The serving path loads vectors once, keeps likes in memory, computes a preference vector through pybind11 and sorts cosine scores in C++. The design makes the algorithm inspectable, but the global like list, duplicate weighting, missing input checks and GIL policy are important next improvements. It is pretrained-model inference and retrieval, not a training pipeline or measured high-performance recommender.

## Skills and interview topics

C++17, HTTP/JSON, vector similarity, CLIP inference, pybind11, Python interpreter lifecycle, locking versus GIL, O(ND + N log N) retrieval, cold start, data portability and evaluation design. Best framing: recommendation prototype or C++/Python integration; secondary evidence for ML infrastructure interests.

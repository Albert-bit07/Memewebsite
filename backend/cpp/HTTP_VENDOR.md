# Vendored HTTP library

`httplib.h` is an unmodified copy of [cpp-httplib v0.63.0](https://github.com/yhirose/cpp-httplib/releases/tag/v0.63.0), from commit `5bcfea7b90575c80a3e54a6967b11b39afa60d5c`. The upstream MIT license is retained in `httplib.LICENSE`.

| File | SHA-256 |
| --- | --- |
| `httplib.h` | `9021001a719768893a8a6e4051c56107690e8b8e49b41e5f903534fa19ae9afb` |
| `httplib.LICENSE` | `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b` |

This replaces version 0.28.0 to incorporate upstream request-framing, header-validation and static-path fixes. The application uses the plain HTTP server; it does not enable the library's TLS or WebSocket client features. Updating a component does not establish that every upstream advisory was reachable in this application.

When refreshing this copy, review upstream behavior changes, retain the license, record the release commit and hashes, and run the native build, ranking tests, frontend contract tests and both HTTP lifecycle scripts. In particular, preserve session identity, restart persistence and catalog-reload consistency. Keep upstream code unmodified so future comparisons remain straightforward.

The server has no built-in TLS deployment configuration. Compatibility tests exercise local HTTP; they do not establish production load capacity or a complete security audit of the library.

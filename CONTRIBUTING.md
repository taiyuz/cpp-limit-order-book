# Contributing

This is [Taiyu Zhu](https://github.com/taiyuz)'s recruiting-portfolio matching
engine. Issues and PRs that tighten correctness, tests, or the documented design
are welcome. Please do not turn it into a full exchange.

## Build and test

CMake 3.24+, a C++23 compiler (GCC 14 / Clang 18), and Ninja. GoogleTest is
pulled with FetchContent.

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

Sanitizer build (the same ASan/UBSan path CI runs on `ubuntu-24.04`):

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan --output-on-failure
```

Style is `.clang-format` (Google, 4-space indent, 100-column). The library and
tests compile with `-Wall -Wextra -Werror`.

## Scope

**In:** limit (GTC) and market matching, cancel, modify, replace (new id, same
side), book invariants, debug snapshot of resting levels.

**Out:** iceberg / hidden / auctions, IOC/FOK flags, stop orders, accounts and
self-trade prevention, persistence, multi-instrument routing, sharing one
`MatchingEngine` across threads.

See [DESIGN.md](DESIGN.md) for layout, complexity, and the lock-free sketch.

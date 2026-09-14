# Condensation regression tests

This standalone harness compiles the production `pbc_condense.cpp` and memory
parser with test doubles for AzerothCore, the database and the LLM. No server,
API key, network request or real database write is needed.

## Run on Linux or Windows

Requires CMake 3.16+ and a C++17 compiler (GCC, Clang or MSVC).

```sh
cmake -S tests/condensation -B build/condensation -DCMAKE_BUILD_TYPE=Debug
cmake --build build/condensation --config Debug --parallel 2
ctest --test-dir build/condensation -C Debug --output-on-failure
```

On GCC/Clang, add `-DPBC_TEST_SANITIZERS=ON` when configuring to enable
AddressSanitizer and UndefinedBehaviorSanitizer. Warnings are treated as errors.
The GitHub Actions workflow runs GCC, Clang and MSVC; each test has a timeout.

## Coverage

- Complete valid batches, score boundaries, French UTF-8 and CRLF.
- Empty, malformed or partially valid output; invalid scores and separators.
- 30-memory limit and SQL TEXT byte boundaries, including multibyte text.
- Missing prompts/connection, LLM failure and empty/missing/stale history.
- Edits, appends, resets and replacement IDs while the request is outstanding.
- Exact memory content, shared ownership, duplicate condensation attempts.
- A new message arriving after the transaction and cache update.
- Unconfirmed persistence keeps history and suspends retries for the character.

Mutations during a request are injected deterministically. These tests validate
the guard logic, not real thread scheduling, locks across the whole module or
the production SQL adapter. The parser deliberately rejects the entire batch
before writes when any line is invalid. Empty extraction keeps history.

## Merge requirements and remaining limits

Inspect the CI result for the exact PR head, not an older successful run.
Before merging, also compile against the intended AzerothCore/Playerbots
revision and run real-database tests. Record those revisions and results in
the PR.

Condensation now commits its memory inserts and exact source cleanup together,
and updates caches only on confirmation. This requires the companion core fix
which checks START/COMMIT and prohibits reconnection replay inside a transaction.
An unconfirmed result suspends condensation for the affected character until
process restart, when authoritative database contents are reloaded.

These doubles do not validate the production database driver. See the separate
local MySQL validation report. Legacy card migration and other history mutation
APIs are outside this change. Syntax validation cannot identify hallucinations
or truncation ending on an otherwise valid line. Configuration reload and global
concurrency are not proven safe by this harness.

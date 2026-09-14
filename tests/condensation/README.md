# Condensation regression tests

This harness compiles the real `src/events/pbc_condense.cpp` and memory parser,
with small test doubles for AzerothCore, the database and the LLM.
It does not require a running game server or API key.

From the repository root, with a C++17 compiler:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -Itests/condensation/stubs -Isrc/events \
  tests/condensation/condensation_test.cpp src/events/pbc_condense.cpp \
  -o /tmp/pbc-condensation-test
/tmp/pbc-condensation-test
```

Covers malformed/partial output, empty replies, invalid scores, batch and TEXT
size limits, French UTF-8/CRLF, missing configuration, LLM failure, stale
snapshots, edits/appends/resets/replaced IDs during extraction, shared history
and a new message arriving during cleanup.

The parser deliberately accepts only complete batches of 1–30 lines with
scores 1–10. Invalid batches produce no writes. No "empty memories" sentinel
is introduced: empty extraction retains history for retry.

This is not a database integration test. The existing database API does not
report insertion success to this code. Atomic persistence and handling SQL
failures remain a separate follow-up. The harness must be complemented by an
AzerothCore/Playerbots build and live database tests before merging.

Initial validation status: source reviewed; execution unavailable in the
authoring environment because its local process runner failed to start.

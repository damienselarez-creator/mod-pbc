# Technical foundation regression tests

Requires CMake, a C++17 compiler, OpenSSL development libraries and the `openssl`
command. Configure with `cmake -S tests/foundation -B build/foundation`, build with
`cmake --build build/foundation --config Debug`, then run
`ctest --test-dir build/foundation -C Debug --output-on-failure`.
Windows may require OpenSSL's `bin` directory on PATH.

The harness compiles unchanged copies or extracts of production code:

- OpenAI-compatible, Anthropic and Ollama response contracts: completed text,
  refusal/tool/truncation/error rejection, no automatic retry, non-streaming
  override and request deadline cap.
- Real loopback HTTP: successful response, error status, redirects, oversized
  response, slow response, invalid host/port.
- Real loopback HTTPS: rejected untrusted certificate, accepted explicit CA,
  rejected hostname mismatch. Certificates exist only in the build directory.
- Event admission: 64 reactions maximum, durable source recording before
  admission, persistence failure/backlog, event expiry and shutdown.
- Connection snapshots surviving concurrent registry replacement.
- Regeneration publication: confirmed complete batch, failed writes, edits,
  newly appended messages, removed history and incomplete generation.

Avast may replace loopback certificates with an untrusted self-signed chain.
The TLS test explicitly reports this as **skipped/inconclusive**, never successful.
It does not disable verification, install certificates or modify antivirus settings.
Set `PBC_REQUIRE_TLS_FIXTURE=1` to treat interception as failure; CI does this.
Other test failures are never skipped. The optional `transport_test public`
smoke test reaches OpenAI's HTTPS endpoint without a key and expects HTTP 401;
it sends only `{}` and cannot generate a paid completion. It is not run by CTest.

`database_scenarios.inc` contains the additional scenarios exercised by the real
AzerothCore/MySQL recovery harness: insert/delete failure during legacy migration,
retry without duplication, unchanged source text, 256-row batches, complete cache
reload and failure on the second history update during regeneration. These are
integration scenarios, not standalone CTest tests. Run only against a disposable
database using the checked-transaction core implementation.

The tests do not simulate complete game-thread scheduling or prove end-to-end
delivery in the running game. Full worldserver compilation and a controlled
in-game pilot are separate validation steps.

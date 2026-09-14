# Technical foundation contracts

## Accepted events and delivery

Incoming chat/narrator sources reach the durable history journal before reaction
queue admission. Database failure retains accepted journal records for ordered
recovery and suppresses the associated reaction. A full reaction queue drops the
reaction after recording its source. Failure before successful local journal
flush is logged and cannot be represented as a durably accepted exchange.

The event queue holds at most 64 items. HTTP whisper/party/trigger queues and the
secondary-event queue each hold at most 64 requests; rejected HTTP admission
returns 429. In-game delivery holds at most 512 actions. Maintenance shares the
event queue and does not have the dialogue expiry deadline.

Dialogue, summaries, regeneration and their secondary reactions expire 60 seconds
after event creation. Foreground LLM calls use the remaining deadline. Delivery
actions also expire. Game state is captured again on the main thread at dequeue;
history is refreshed immediately before each reply. History changes while a reply
is generated reject it, including a final comparison under the persistence lock.
This does not monitor every live game-state change during a network call.

Replies are persisted before in-game delivery and history notification. No second
unconfirmed UI preview is sent after the confirmed history row. Regeneration
validates the original history again and commits all replacements together before
cache publication or delivery. Incomplete/stale/failed replacements retain the
previous cache. An uncertain COMMIT still requires recovery/reload to reconcile.

The worker is joined during shutdown while database services remain available.
Only the worker wrapper marks it done, including after exceptions. Configuration
reload refuses while an event is running. Connection lookups return immutable
owned copies, so an outstanding request survives replacement of the registry.

## HTTP and LLM responses

HTTPS verifies the certificate chain and hostname. `PBC.HttpCaFile` optionally
selects an operator-provided CA bundle; there is no insecure fallback. The default
uses the TLS library's system trust support. Redirects are disabled. HTTP responses
are limited to 2 MiB; request timeouts are clamped to 1–120 seconds and the connection
timeout to at most 10 seconds. DNS/system TLS checks may have platform-specific
timing outside socket deadlines. There is no automatic retry after an uncertain
request, avoiding duplicated latency or paid work.

Requests force `stream=false` after custom parameters. Complete text requires
OpenAI-compatible `finish_reason=stop`, Anthropic `end_turn`/`stop_sequence`, or
Ollama `done=true` and `done_reason=stop`. Missing metadata, truncation, unsupported
tool/refusal termination, malformed JSON and empty text fail closed. Servers that
omit this metadata require an adapter; partial text is never used as a memory.
See the [OpenAI completion reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create),
[Anthropic stop reasons](https://platform.claude.com/docs/en/build-with-claude/handling-stop-reasons)
and [Ollama chat response](https://docs.ollama.com/api/chat).

Account-scoped history edit/delete requires ownership of the addressed history
row and authorization over every owner affected by a shared-row mutation. Missing
authorization returns 403 before a database write. Removing just one's own
ownership remains the way to forget a message shared with another account.

## Legacy memories

The console migration transfers original addition text verbatim with neutral
importance 5. Each transaction inserts at most 256 source rows and consumes those
same rows. A failed insert or source deletion rolls back the complete batch;
retry after a committed batch cannot duplicate it. Both tables must be InnoDB.
Each confirmed batch is followed by a complete cache reload. Extraction or
reinterpretation by a model is a separate operation, not part of safe migration.

## Validation and operational limits

Use `tests/foundation`, the existing history/mutation/condensation/journal tests,
and the real-core MySQL scenarios. Full worldserver compilation is required.
The Linux CI job requires a working controlled TLS fixture; antivirus interception
is reported explicitly by local tests and is not a successful result.

Journal flushes, SQL confirmation and complete cache reload still wait synchronously
under locks. These operations may delay the main game loop during database/storage
slowness; reaction deadline limits do not eliminate this latency. Physical storage
loss, pre-flush failures, durable edit/delete intentions and exact in-game delivery
after a crash are outside the history journal's guarantee. Receipt garbage
collection is not implemented. Follow RECOVERY.md for deployment and backups.

This version hardens the existing architecture. Hybrid provider routing and
character personality work are not implemented by these changes.

# Memory persistence contract

This change requires the companion AzerothCore MySQLConnection and DatabaseWorkerPool patches. It must
not be deployed by updating mod-pbc alone on an unpatched core.

The core checks START TRANSACTION and COMMIT, reports their errors, and refuses
to replay an individual statement after reconnecting within a transaction. A
subsequent operation outside the transaction may reconnect normally.

Condensation validates the entire LLM response and checks its source snapshot.
It then inserts every memory and removes only the captured history ownerships
in one InnoDB transaction. Orphan cleanup is in that same transaction; shared
messages are retained for other characters. Caches change only after the database
worker confirms completion.

If persistence is not confirmed, history stays in RAM and the character is
suspended from further condensation until the server process is restarted.
This includes uncertain COMMIT outcomes: automatic retry might duplicate a batch
that was actually committed. After restart, database loading reconciles state.
There is deliberately no timer-based retry or hot-reload reset of this latch.

The event worker waits for the database while holding history and memory locks.
This preserves ordering with the existing cache mutation APIs, but slow database
operations can delay users of those locks. Moving this work fully off shared
locks requires a separate versioned persistence protocol; it is not solved here.

Ordinary history insertion reserves one synchronous database connection for the
message and all deduplicated owners. The first generated ID is captured on that
connection and returned only after a successful COMMIT. Any statement failure
rolls back the whole transaction. Cache publication and notifications follow
confirmation, under the history lock, which also serializes duplicate checks.
Time-gap insertion uses this same path and reports unconfirmed insertion as false.

An unconfirmed ordinary insertion returns zero and logs a warning. There is no
durable retry journal yet: a rejected write can lose the incoming exchange, and
an uncertain COMMIT can leave a stored message absent from RAM until reload.
Automatic retries require an idempotency mechanism to avoid duplicates.

Remaining separate work includes durable recovery of incoming exchanges,
migration of legacy card additions, edit/reset persistence,
LLM truncation detection, and bounded event processing. This patch does not
establish a lossless guarantee for every module operation.

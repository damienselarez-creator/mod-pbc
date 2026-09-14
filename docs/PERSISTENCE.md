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

History, memory and relationship edits/deletions now wait for a confirmed
transaction before changing their caches. Hard history deletion and ownership
removal include orphan cleanup in the same transaction. The API returns HTTP 503
with `persistence_unconfirmed` when confirmation fails; it does not report success.

Character reset and global reset delete history ownerships, orphan history,
memories and relationships in one transaction while holding all three cache
locks. Individual reset preserves shared messages owned by another character.
The command reports failure and retains caches if persistence is unconfirmed.
The obsolete independent reset helpers have been removed.

Generated relationships also publish only after confirmation. A process-local
generation captured in character snapshots rejects results queued before a reset
attempt or manual relationship mutation; current relationship text must match too.
The generation is intentionally global, so such an action may also discard an
unrelated queued relationship update. It is not a general event cancellation
system: other queued dialogue and legacy migration work are not covered.

An uncertain mutation COMMIT can leave the database ahead of the retained cache.
HTTP 503 must not be interpreted as proof of rollback. Reconciliation after an
uncertain acknowledgement, loader failures and durable incoming-write recovery
remain separate work. Synchronous waits can delay the world thread during reset.
These operations assume module-owned rows are not independently edited in SQL.

Remaining separate work includes durable recovery of incoming exchanges,
migration of legacy card additions, safe cache reloading,
LLM truncation detection, and bounded event processing. This patch does not
establish a lossless guarantee for every module operation.

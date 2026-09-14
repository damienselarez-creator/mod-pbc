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

Ordinary history insertion now writes an immutable local journal record before
attempting SQL. The record contains its token, timestamp, message and owners; a
checksum detects damage. File contents are flushed before atomic publication.
The journal directory is exclusively locked for the process lifetime.

SQL stores the token receipt, message and owners in one transaction. A replay of
an existing receipt performs no message or ownership writes. Receipts outlive
history deletion and condensation, including global reset, so an old journal
cannot resurrect intentionally removed exchanges. Do not purge receipts without
an explicit retention protocol covering all journal copies and backups.

A journal record is removed only after confirmed SQL persistence. An interrupted
or uncertain write stays queued. Later exchanges are also journaled and deferred
until recovery, preserving arrival order. Recovery runs at startup and during
`.chars reload`; it is not a timer-based retry worker. The monotonic journal order
also tolerates a wall-clock rollback. No success or history notification is
published for a merely queued exchange.

A complete reload holds all four cache locks, replays pending records, then reads
history/ownership, memories, relationships and roll modifiers in one statement.
A sentinel distinguishes empty tables from read failure. Caches are prepared
separately and swapped only after complete validation; dangling ownership is
rejected. An unsuccessful read or recovery retains the previous caches.
Roll modifier writes now confirm persistence under their cache lock too.

The administrative reload reports its actual synchronous result. Startup stops
module initialization if recovery or loading fails. Receipt-table absence is
checked without querying the missing table directly. Apply the SQL migration
before deployment; the core can abort on other incompatible/missing schema.

See RECOVERY.md for migration, journal configuration and recovery operation.

History, memory and relationship edits/deletions now wait for a confirmed
transaction before changing their caches. Hard history deletion and ownership
removal include orphan cleanup in the same transaction. The API returns HTTP 503
with `persistence_unconfirmed` when confirmation fails; it does not report success.

Character reset and global reset delete history ownerships, orphan history,
memories and relationships in one transaction while holding all three cache
locks. Individual reset preserves shared messages owned by another character.
The command reports failure and retains caches if persistence is unconfirmed.
Reset is refused while journal recovery remains pending; reload first.
The obsolete independent reset helpers have been removed.

Generated relationships also publish only after confirmation. A process-local
generation captured in character snapshots rejects results queued before a reset
attempt or manual relationship mutation; current relationship text must match too.
The generation is intentionally global, so such an action may also discard an
unrelated queued relationship update. It is not a general event cancellation
system: other queued dialogue and legacy migration work are not covered.

An uncertain mutation COMMIT can leave the database ahead of the retained cache.
HTTP 503 must not be interpreted as proof of rollback. A successful complete
reload reconciles the caches with the committed state. Edit/delete intentions
are not themselves stored in the incoming-history journal.

Remaining work includes legacy card-addition migration, bounded event processing,
LLM truncation detection, and reducing synchronous waits under shared locks.
The journal cannot guarantee an exchange that never reached a successful local
flush, or recover storage that was physically lost. Windows process-crash tests
are covered; physical power loss and the POSIX implementation were not exercised.
This patch does not establish a lossless guarantee for every module operation.

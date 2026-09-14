# History recovery deployment and operation

1. Stop the game server before deploying this version. Keep the previously
   required checked-transaction core patch.
2. Back up the characters database. Apply
   `data/sql/characters/updates/migration-20260914-history-receipts.sql` to that
   same database. This adds an InnoDB receipt table; it does not rewrite history.
3. Set `PBC.HistoryJournalPath` to a persistent writable directory dedicated to
   this server and database. The default is `./pbc-journal`, relative to the
   server working directory. Prefer an absolute path. Its contents include
   dialogue text; protect it as you protect the database.
4. Start the server and check that complete memory-cache loading succeeded.
   An unavailable journal, failed recovery, or missing receipt migration blocks
   initialization instead of accepting an empty memory state.

The directory is locked to one process. Changing its configured path requires a
restart. Do not point another server/database at it, remove pending files to
silence an error, or clear receipts during reset. Keep the database and journal
backups matched; replay against an unrelated or older database is not a supported
restore procedure.

After a temporary database failure, exchanges that were durably journaled can be
recovered with `.chars reload` when the event worker is idle, or automatically at
the next startup. Later
exchanges stay queued behind them until recovery. Reset is refused while recovery
is pending. Read the command result and server log; a queued exchange has not yet
been published in the live history cache.

A `.pending` record is eligible for replay. A checksum or parse failure stops
recovery and retains the caches; preserve the damaged file for investigation.
A `.tmp` file left by a crash was not successfully published as an accepted
record and is not replayed automatically. Do not rename it to force replay.
Receipts prevent reapplying a completed record, even if its message was later
condensed or deleted. No automatic receipt garbage collection is implemented.

Disk flushes, SQL confirmation, and full reloads are synchronous and can delay
users of the corresponding locks. Reaction deadlines and bounded queues are
described in TECHNICAL_FOUNDATION.md; database/storage waits remain synchronous. Durability relies on the filesystem and storage honoring flushes;
physical media loss and failures before a successful local flush are outside
this guarantee. Legacy migration is independently transactional. Durable edit/delete
intentions are not covered by the incoming-history journal.

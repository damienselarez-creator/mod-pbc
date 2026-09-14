# Durable journal tests

Configure this directory with CMake and an OpenSSL installation, build, then run
CTest (`-C Debug` for Visual Studio). The fixture starts a writer process that
flushes a record and exits without destructors. A separate process validates
recovery, exclusive directory locking, checksum rejection, acknowledgement and
ordering even when an existing record appears to come from the future.

The test writes only under its build directory. It does not simulate physical
power loss. MySQL integration is tested separately against the patched core pool:
transaction replay, rollback, reset receipts, ordered backlog, complete/empty/
failed cache reads and process exits before SQL and after COMMIT.

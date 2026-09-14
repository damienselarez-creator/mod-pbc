# Mutation publication tests

Configure this directory with CMake, build and run CTest (`-C Debug` with Visual
Studio). No game server or database is required.

The test extracts the production cache mutation functions and HTTP result mapper.
A controlled database stub verifies that failed persistence does not change the
caches, confirmed deletion handles shared ownership, individual/global reset
updates the intended caches, stale generated relationships are rejected, and
unconfirmed mutations produce HTTP 503 instead of success.

SQL rollback is validated separately with the real patched core pool against an
isolated MySQL instance. These unit tests do not establish durable recovery after
lost acknowledgements or a process crash.

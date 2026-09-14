# History publication regression test

Configure this directory with CMake, build, then run CTest (select the build
configuration with `-C Debug` for Visual Studio). No running game or database is
required.

The test extracts the production append and time-gap functions, replacing their
database dependency with a controlled stub. It checks failed-write publication,
owner deduplication, concurrent duplicate suppression, and time-gap results.
It does not validate SQL, connection pooling, or durable recovery. Those require
the paired core patch and integration tests against an isolated MySQL instance.

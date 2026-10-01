# Bundled cpp-httplib

- Upstream: https://github.com/yhirose/cpp-httplib
- Release: v0.58.0
- Commit: `4f3f9ef19be83ae97a5d9a059432dc00e445b7ab`
- Imported files: `httplib.h` unchanged from that commit; `LICENSE` has only its extra trailing blank line removed.
- License: MIT, copyright Yuji Hirose and contributors; see `LICENSE`.

Updated from v0.43.1 on 2026-09-28. This includes the upstream fixes for
GHSA-xjxg-64p4-vj4m (HTTP header percent decoding) and GHSA-h6wq-j5mv-f3q8
(negative chunk sizes), alongside subsequent upstream fixes.

PBC still renames the library namespace to `pbc_httplib` at include sites to
avoid collisions with other modules. Do not edit the vendored namespace or
disable certificate/hostname verification to accommodate an update.

Validation lives in `tests/foundation`: HTTP response/error/size/timeout policy,
TLS trust and hostname checks, raw malformed chunk rejection, literal header
values, and WS/WSS handshake, subprotocol and message exchange. The WebSocket
fixture checks library API compatibility; it does not authenticate against a
running AzerothCore database or replace an in-game PBC UI test.

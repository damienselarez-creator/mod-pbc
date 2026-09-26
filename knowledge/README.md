# Historical corpus retrieval

`histoire-reprouves.json` is the historical pillar supplied by the user, not the character's cultural doctrine, personal biography or lived memory. Its sources are references from the supplied manuscript, not newly audited canon. The corpus ends before the Wrathgate and the battle for Undercity.

Configure `PBC.HistoryCorpusPath`, `PBC.HistoryCharacterGuids` and `PBC.HistoryAllowedChunks` explicitly. GUIDs and chunk IDs are comma-separated; wildcard access is not supported. Empty path disables retrieval. Unknown IDs, malformed JSON, unsupported version/timeline, missing fields, invalid limits and duplicate IDs reject the replacement and retain the complete prior corpus and access policy. A first-load failure leaves historical retrieval unavailable while normal dialogue remains functional; the server logs an error. Use `.chars reload` while the event worker is idle to reload. `chars lore-test 5 Arthas ou Keltuzad ?` in the server console inspects local selection without calling an API or modifying memory. In-game, prefix the command with a dot (GM access required).

The loader precomputes a small lexical index at startup/reload and publishes an immutable snapshot. Queries use only the triggering event (message or event summary), capped at 8192 bytes. Explicit entities, accent/punctuation normalization, a few aliases and keywords rank candidates; ties are deterministic. This is not a general semantic search and does not resolve pronouns from preceding turns. Queries never read files or call another model. A selected block is appended after dialogue template substitution, including regenerated dialogue, but not to condensation, relationship extraction or stored history. The bot's spoken answer can subsequently be remembered as an actual conversation.

The default caps are four complete chunks and 6000 UTF-8 bytes, including documentary labels. Limits are bytes, not exact model tokens. Chunks that do not fit are skipped whole; no partial UTF-8 or sentence truncation. The corpus file is limited to 2 MiB, 512 chunks and nesting depth 32. The global allowlist applies to all configured characters. An optional non-empty character_guids array on each chunk restricts it further to the listed numeric GUIDs. Omitted arrays preserve legacy behavior; malformed, duplicate, zero or globally unauthorized GUIDs reject the replacement atomically.

Pilot profile: Anthanagor, character GUID 5. Initially exclude `prise-capitale`, `bastions-fleau`, `couronne-volonte`, `nouvelle-peste`, and `souvenirs-des-morts` (all IDs prefixed with `reprouves.histoire.`). These contain private intentions, concealed identities, captivity or intimate accounts whose transmission to Anthanagor has not been established. The 22 other chunks are available as historical accounts, never personal memories. This is a conservative initial policy, not a claim that every public detail is independently verified. The policy can be refined when his background is written.

Standalone production-reader tests:

```text
cmake -S tests/lore -B build-lore
cmake --build build-lore --config Release
ctest --test-dir build-lore -C Release --output-on-failure
```

No SQL migration is required. Historical knowledge does not write to the memory tables or journal. Data files must remain controlled by the server administrator; quoted documentary data and prompting are not a security boundary against malicious corpus authors.


Deployment 2026-09-20: compagnons-documentaire.json preserves Antanagor (GUID 4, 50 allowed chunks) and adds Elvidia (GUID 5, 131 historical chunks). Every chunk has an explicit character_guids restriction. Elvidia bibliographic and late-WotLK passages remain in the local source archive, outside runtime selection. Knowledge is documentary context, never personal memory. The 181/186 load leaves five pre-existing Antanagor exclusions unchanged.

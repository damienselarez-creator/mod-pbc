# Commands

List of commands that can be used by the player or in the server console.

| Command | Description |
|---|---|
| `.chars reload` | Reloads module config, prompts, character cards, and data. Also reloads history and relationships from the database after all pending events are processed. |
| `.chars condense <name>` | Condenses a character's history into discrete narrator-style memories and clears all history. Also triggers relationship updates for party members. |
| `.chars info <name>` | Prints the character's card, memory count, history length, estimated tokens, and roll modifier. |
| `.chars reset <name>` | Removes all memories, history, and relationship data for a character. |
| `.chars reset @ALL` | Removes all memories, history, and relationship data for every character. |
| `.chars history <name> [num=5]` | Shows the last `num` entries from a character's chat history (max 20). |
| `.chars relationship <name> <target>` | Shows a character's LLM-generated relationship description towards another character. |
| `.chars relationship-update <name> <target>` | Queues an immediate relationship update LLM call for a character towards a target. |
| `.chars roll-modifier <name> [value]` | Sets or displays the per-character roll chance modifier (−100 to 100). Positive = more talkative, negative = less talkative. Does not affect whisper or mention chances. Omit value to display current. |
| `.chars context <name>` | Prints the character's current context variable with all template substitutions applied (useful for debugging). |
| `.chars web` | Generates a one-time password for the web interface and displays the connection URL. In-game only. |
| `.chars connection-test [connection_name]` | Sends a test request to the specified connection (`default`, `utility`, `condensation`, or `relationship`) and prints the response. Uses `default` if omitted; task-specific slots that aren't individually configured fall back to `default` automatically.|
| `.chars narrate <name> <message>` | Adds a narrator line to a character's chat history. Does not trigger a response. In-game only. |
| `.chars narrate-party <message>` | Adds a narrator line to every group character's chat history. Does not trigger responses. In-game only. |
| `.chars trigger <name>` | Forces a character to respond (party message if grouped, say otherwise). The trigger event is not written into history. Can also trigger the player's own character. |
| `.chars regen-last` | Regenerates the responses of the last event that produced character replies. The same characters respond again in the same order — their original messages are edited in place. Only available when no new messages have been added to any affected character's history since the original event, and the caller is in the same group as the event's characters. In-game only. |
| `.chars migrate-card-additions` | Console-only. Transfers original legacy addition texts into memories, in atomic batches of at most 256 rows. Each confirmed batch consumes its source rows; retries do not duplicate them. The old table itself remains. |

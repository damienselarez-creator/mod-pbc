#include "test_environment.h"
#include "pbc_condense.h"
#include "pbc_memory_parser.h"
#include "pbc_database.h"
#include <cstdlib>
#include <iostream>

void Check(bool ok, const char* description)
{
    if (!ok) { std::cerr << "FAIL: " << description << "\n"; std::exit(1); }
}

PBC_CharacterSnapshot Reset()
{
    g_PBC_History = {{11, {"Norka: Help!"}}, {12, {"You: I am here."}}};
    g_PBC_HistoryOwners = {{1, {11, 12}}, {2, {11, 12}}};
    g_PBC_LastHistoryTime = {{1, 123}};
    g_PBC_Memories.clear();
    g_PBC_Relationships.clear();
    g_PBC_RelationshipUpdateSystemPrompt.clear();
    g_PBC_RelationshipUpdateUserPrompt.clear();
    deletedIds.clear(); queued.clear();
    calls = writes = 0;
    duringRequest = {}; duringDelete = {};
    connectionAvailable = true;
    response = {true, "[7] You helped Norka."};
    PBC_CharacterSnapshot snap;
    snap.history = {"Norka: Help!", "You: I am here."};
    return snap;
}

int main()
{
    auto parsed = PBC_ValidateMemoryLines("  [1] Low.\r\n\n[10] Très important.  \r\n");
    Check(parsed.size() == 2 && parsed[1].importance == 10
        && parsed[1].text == "Très important.", "valid UTF-8, CRLF and whitespace");
    for (const std::string invalid : {"", "   ", "No memories.", "[0] bad", "[11] bad",
        "[999999999999999999999] bad", "[7]   ", "[7] valid\ninvalid",
        "```\n[7] valid\n```", "[7] valid\n[3]"})
    {
        auto snap = Reset();
        response.text = invalid;
        Check(!PBC_CondenseInline(snap, "system", "user"), "invalid batch rejected");
        Check(writes == 0 && deletedIds.empty() && snap.history.size() == 2
            && g_PBC_HistoryOwners.at(1).size() == 2, "invalid batch has no side effects");
    }
    for (const std::string invalid : {"[] Missing score", "[01] Leading zero",
        "[-1] Negative", "[+1] Signed", "[1.0] Decimal", "[1]No separator", "[1"})
        Check(PBC_ValidateMemoryLines(invalid).empty(), "invalid score or separator");
    Check(PBC_ValidateMemoryLines("[5] " + std::string(65535, 'x')).size() == 1,
        "SQL TEXT exact byte boundary accepted");
    std::string unicodeText;
    for (int i = 0; i < 32768; ++i) unicodeText += u8"é";
    Check(PBC_ValidateMemoryLines("[5] " + unicodeText).empty(), "UTF-8 byte limit");
    std::string thirty;
    for (int i = 0; i < 30; ++i) thirty += "[5] Memory.\n";
    Check(PBC_ValidateMemoryLines(thirty).size() == 30, "thirty accepted");
    Check(PBC_ValidateMemoryLines(thirty + "[5] Extra.").empty(), "overflow batch rejected");
    Check(PBC_ValidateMemoryLines("[5] " + std::string(65536, 'x')).empty(), "SQL TEXT overflow rejected");

    auto snap = Reset();
    response.success = false;
    Check(!PBC_CondenseInline(snap, "system", "user") && writes == 0 && deletedIds.empty(), "LLM failure retains history");
    snap = Reset();
    connectionAvailable = false;
    Check(!PBC_CondenseInline(snap, "system", "user") && calls == 0, "missing connection");
    snap = Reset();
    Check(!PBC_CondenseInline(snap, "", "user") && calls == 0, "missing prompt");

    snap = Reset();
    snap.history.clear();
    Check(!PBC_CondenseInline(snap, "system", "user") && calls == 0, "empty snapshot");
    snap = Reset();
    snap.history.pop_back();
    Check(!PBC_CondenseInline(snap, "system", "user") && calls == 0, "stale queued snapshot");
    snap = Reset();
    g_PBC_History.erase(12);
    Check(!PBC_CondenseInline(snap, "system", "user") && calls == 0, "missing source row");

    for (int mutation = 0; mutation < 4; ++mutation)
    {
        snap = Reset();
        duringRequest = [mutation] {
            if (mutation == 0) {
                g_PBC_History[13] = {"New exchange"};
                g_PBC_HistoryOwners[1].push_back(13);
            } else if (mutation == 1) {
                g_PBC_History[11].message = "Edited exchange";
            } else if (mutation == 2) {
                g_PBC_HistoryOwners.erase(1);
            } else {
                g_PBC_History[21] = g_PBC_History.at(11);
                g_PBC_HistoryOwners[1][0] = 21;
            }
        };
        Check(!PBC_CondenseInline(snap, "system", "user"), "concurrent mutation rejected");
        Check(writes == 0 && deletedIds.empty() && snap.history.size() == 2, "concurrent mutation has no condensation writes");
    }

    snap = Reset();
    Check(PBC_CondenseInline(snap, "system", "user"), "unchanged history succeeds");
    Check(g_PBC_Memories.at(1).at(0).text == "You helped Norka."
        && g_PBC_Memories.at(1).at(0).importance == 7, "memory content preserved");
    Check(!g_PBC_HistoryOwners.count(1) && !g_PBC_LastHistoryTime.count(1),
        "completed owner history removed");
    Check(!PBC_CondenseInline(snap, "system", "user") && writes == 1,
        "same snapshot cannot be condensed twice");

    snap = Reset();
    response.text = "[7] First memory.\n[8] Second memory.";
    duringDelete = [] {
        g_PBC_History[13] = {"Arrived after extraction"};
        g_PBC_HistoryOwners[1].push_back(13);
        g_PBC_LastHistoryTime[1] = 456;
    };
    Check(PBC_CondenseInline(snap, "system", "user"), "valid extraction succeeds");
    Check(writes == 2 && g_PBC_Memories.at(1).size() == 2, "both memories saved");
    Check(deletedIds == std::vector<uint64_t>({11, 12}), "only captured source IDs deleted");
    Check(g_PBC_HistoryOwners.at(1) == std::deque<uint64_t>({13})
        && g_PBC_LastHistoryTime.at(1) == 456, "late message retained");
    Check(g_PBC_HistoryOwners.at(2) == std::deque<uint64_t>({11, 12}), "other owner's history retained");
    Check(snap.history.empty(), "completed snapshot cleared");

    snap = Reset();
    snap = Reset();
    commitSucceeds = false;
    const int previousCommits = commitCalls;
    Check(!PBC_CondenseInline(snap, "system", "user"), "failed persistence rejected");
    Check(writes == 0 && deletedIds.empty() && snap.history.size() == 2
        && g_PBC_HistoryOwners.at(1).size() == 2 && g_PBC_Memories.at(1).empty(),
        "unconfirmed commit preserves history and memory contents");
    commitSucceeds = true;
    const int previousCalls = calls;
    Check(!PBC_CondenseInline(snap, "system", "user") && calls == previousCalls
        && commitCalls == previousCommits + 1, "uncertain commit is never retried in this process");
    std::cout << "Condensation regression tests passed.\n";
}

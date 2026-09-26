#include "pbc_selfbot_policy.h"
#include <cstdlib>

void Check(bool condition)
{
    if (!condition)
        std::abort();
}

int main()
{
    // A manually connected character is not enough: only its own active AI can speak.
    for (unsigned mask = 0; mask < 16; ++mask)
    {
        bool bot = mask & 1;
        bool socket = mask & 2;
        bool logout = mask & 4;
        bool selfAI = mask & 8;
        Check(PBC_SelfbotEligible(bot, socket, logout, selfAI) == (mask == 10));
    }
    Check(PBC_QuestActorEligible(true, false, false, true));  // solo selfbot
    Check(PBC_QuestActorEligible(true, true, false, true));   // non-leader selfbot
    Check(!PBC_QuestActorEligible(false, false, false, true));
    Check(!PBC_QuestActorEligible(false, true, false, true));
    Check(PBC_QuestActorEligible(false, true, true, true));   // existing companion path
    Check(!PBC_QuestActorEligible(false, true, true, false));
    Check(PBC_SelfbotReplyAllowed(true, true));
    Check(!PBC_SelfbotReplyAllowed(true, false));             // manual handoff / disconnect
    Check(PBC_SelfbotReplyAllowed(false, false));            // explicit non-selfbot actions preserved
}

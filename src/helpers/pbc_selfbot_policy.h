#ifndef MOD_PBC_SELFBOT_POLICY_H
#define MOD_PBC_SELFBOT_POLICY_H

constexpr bool PBC_SelfbotEligible(bool botSession, bool socketOpen, bool loggingOut, bool selfAI)
{
    return !botSession && socketOpen && !loggingOut && selfAI;
}

constexpr bool PBC_QuestActorEligible(bool selfbot, bool grouped, bool leader, bool observerPresent)
{
    return selfbot || (grouped && leader && observerPresent);
}

constexpr bool PBC_SelfbotReplyAllowed(bool requiresSelfbot, bool activeSelfbot)
{
    return !requiresSelfbot || activeSelfbot;
}

#endif

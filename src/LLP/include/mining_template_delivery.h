/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People
__________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_MINING_TEMPLATE_DELIVERY_H
#define NEXUS_LLP_INCLUDE_MINING_TEMPLATE_DELIVERY_H

#include <LLP/include/mining_constants.h>
#include <LLP/include/round_state_utility.h>

#include <chrono>
#include <cstdint>

namespace LLP
{
    enum class TemplatePushDecisionReason : uint8_t
    {
        FIRST_PUSH = 0,
        FORCE_BYPASS,
        CHAIN_TIP_CHANGED,
        INTERVAL_ELAPSED,
        THROTTLED
    };


    struct TemplatePushDecision
    {
        bool fShouldSend = false;
        TemplatePushDecisionReason eReason = TemplatePushDecisionReason::FIRST_PUSH;
        int64_t nElapsedMs = 0;
    };


    inline const char* TemplatePushDecisionReasonCode(TemplatePushDecisionReason eReason)
    {
        switch(eReason)
        {
            case TemplatePushDecisionReason::FORCE_BYPASS:     return "FORCE_BYPASS";
            case TemplatePushDecisionReason::CHAIN_TIP_CHANGED:return "CHAIN_TIP_CHANGED";
            case TemplatePushDecisionReason::INTERVAL_ELAPSED: return "INTERVAL_ELAPSED";
            case TemplatePushDecisionReason::THROTTLED:        return "THROTTLED";
            default:                                           return "FIRST_PUSH";
        }
    }


    inline TemplatePushDecision ApplyTemplatePushThrottle(
        std::chrono::steady_clock::time_point& tLastTemplatePushTime,
        bool& fForceNextPush,
        uint1024_t& hashLastPushedChain,
        const uint1024_t& hashCurrentChain,
        const std::chrono::steady_clock::time_point& tNow = std::chrono::steady_clock::now())
    {
        TemplatePushDecision result;
        const bool fHadPriorPush = (tLastTemplatePushTime != std::chrono::steady_clock::time_point{});
        result.nElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            tNow - tLastTemplatePushTime).count();

        if(!fHadPriorPush)
        {
            fForceNextPush = false;
            tLastTemplatePushTime = tNow;
            hashLastPushedChain = hashCurrentChain;
            result.fShouldSend = true;
            result.eReason = TemplatePushDecisionReason::FIRST_PUSH;
            return result;
        }

        if(fForceNextPush)
        {
            fForceNextPush = false;
            tLastTemplatePushTime = tNow;
            hashLastPushedChain = hashCurrentChain;
            result.fShouldSend = true;
            result.eReason = TemplatePushDecisionReason::FORCE_BYPASS;
            return result;
        }

        if(hashCurrentChain != hashLastPushedChain)
        {
            tLastTemplatePushTime = tNow;
            hashLastPushedChain = hashCurrentChain;
            result.fShouldSend = true;
            result.eReason = TemplatePushDecisionReason::CHAIN_TIP_CHANGED;
            return result;
        }

        if(tLastTemplatePushTime != std::chrono::steady_clock::time_point{} &&
           result.nElapsedMs < MiningConstants::TEMPLATE_PUSH_MIN_INTERVAL_MS)
        {
            result.fShouldSend = false;
            result.eReason = TemplatePushDecisionReason::THROTTLED;
            return result;
        }

        tLastTemplatePushTime = tNow;
        hashLastPushedChain = hashCurrentChain;
        result.fShouldSend = true;
        result.eReason = TemplatePushDecisionReason::INTERVAL_ELAPSED;

        return result;
    }


    struct TemplateRefreshDecision
    {
        bool fUnifiedHeightChanged = false;
        bool fReorgDetected = false;
        bool fTemplateStale = false;
    };


    inline TemplateRefreshDecision EvaluateTemplateRefresh(
        uint32_t nLastTemplateUnifiedHeight,
        const uint1024_t& hashLastBlock,
        const RoundStateUtility::ChainHeightSnapshot& snap)
    {
        TemplateRefreshDecision result;
        result.fUnifiedHeightChanged = RoundStateUtility::IsTemplateStale(
            nLastTemplateUnifiedHeight, snap);
        result.fReorgDetected = RoundStateUtility::IsReorgDetected(hashLastBlock, snap);
        result.fTemplateStale = (result.fUnifiedHeightChanged || result.fReorgDetected);
        return result;
    }
}

#endif

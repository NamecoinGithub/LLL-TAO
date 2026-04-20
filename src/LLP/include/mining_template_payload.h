/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

__________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_MINING_TEMPLATE_PAYLOAD_H
#define NEXUS_LLP_INCLUDE_MINING_TEMPLATE_PAYLOAD_H

#include <LLP/include/get_block_policy.h>
#include <LLP/include/mining_constants.h>
#include <LLP/include/round_state_utility.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/types/block.h>
#include <TAO/Ledger/types/state.h>

#include <Util/include/debug.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace LLP
{
    struct SharedTemplatePayloadResult
    {
        bool fSuccess = false;
        std::vector<uint8_t> vPayload;
        GetBlockPolicyReason eReason = GetBlockPolicyReason::NONE;
        uint32_t nRetryAfterMs = 0;
        uint32_t nBlockChannel = 0;
        uint32_t nBlockHeight = 0;
        uint32_t nBlockBits = 0;
    };


    inline SharedTemplatePayloadResult BuildSharedTemplatePayload(
        TAO::Ledger::Block* pBlock, const char* strLaneLabel)
    {
        SharedTemplatePayloadResult result;

        if(pBlock == nullptr)
        {
            result.eReason = GetBlockPolicyReason::INTERNAL_RETRY;
            result.nRetryAfterMs = MiningConstants::GET_BLOCK_THROTTLE_INTERVAL_MS;
            return result;
        }

        TAO::Ledger::BlockState stateBest = TAO::Ledger::ChainState::tStateBest.load();
        TAO::Ledger::BlockState stateChannel = stateBest;
        uint32_t nChannelHeight = 0;
        if(TAO::Ledger::GetLastState(stateChannel, pBlock->nChannel))
            nChannelHeight = stateChannel.nChannelHeight;

        const std::vector<uint8_t> vBlockData = pBlock->Serialize();
        if(vBlockData.empty())
        {
            debug::error(FUNCTION, strLaneLabel, ": Block::Serialize() returned empty — TEMPLATE_NOT_READY");
            result.eReason = GetBlockPolicyReason::TEMPLATE_NOT_READY;
            result.nRetryAfterMs = MiningConstants::GET_BLOCK_THROTTLE_INTERVAL_MS;
            return result;
        }

        std::vector<uint8_t> vMetadata = RoundStateUtility::SerializeTemplateMetadata(
            static_cast<uint32_t>(stateBest.nHeight), nChannelHeight, pBlock->nBits);

        result.vPayload.reserve(vMetadata.size() + vBlockData.size());
        result.vPayload.insert(result.vPayload.end(), vMetadata.begin(), vMetadata.end());
        result.vPayload.insert(result.vPayload.end(), vBlockData.begin(), vBlockData.end());

        result.fSuccess = true;
        result.eReason = GetBlockPolicyReason::NONE;
        result.nRetryAfterMs = 0;
        result.nBlockChannel = pBlock->nChannel;
        result.nBlockHeight = pBlock->nHeight;
        result.nBlockBits = pBlock->nBits;

        return result;
    }


    template <typename CreateBlockFn>
    inline SharedTemplatePayloadResult BuildSharedTemplatePayloadWithRetry(
        CreateBlockFn&& fnCreateBlock, const char* strLaneLabel)
    {
        TAO::Ledger::Block* pBlock = fnCreateBlock();
        if(!pBlock)
        {
            debug::log(3, FUNCTION, strLaneLabel, ": new_block() returned nullptr, retrying once");
            pBlock = fnCreateBlock();
        }

        if(!pBlock)
        {
            debug::log(3, FUNCTION, strLaneLabel, ": new_block() failed after retry — INTERNAL_RETRY");

            SharedTemplatePayloadResult result;
            result.eReason = GetBlockPolicyReason::INTERNAL_RETRY;
            result.nRetryAfterMs = MiningConstants::GET_BLOCK_THROTTLE_INTERVAL_MS;
            return result;
        }

        return BuildSharedTemplatePayload(pBlock, strLaneLabel);
    }
}

#endif

/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_PUSH_NOTIFICATION_H
#define NEXUS_LLP_INCLUDE_PUSH_NOTIFICATION_H

#include <cstdint>
#include <vector>

/* Forward declarations to avoid heavy includes */
namespace LLP
{
    class Packet;
    class StatelessPacket;
}

namespace TAO
{
    namespace Ledger
    {
        class BlockState;
        class Block;
    }
}

namespace LLP
{
    /** Protocol Lane Enum
     * 
     *  Determines which opcode format to use (8-bit vs 16-bit)
     **/
    enum class ProtocolLane : uint8_t
    {
        LEGACY = 0,      // Port 8323, 8-bit opcodes
        STATELESS = 1    // Port 9323+, 16-bit opcodes
    };

    /** Push Notification Builder
     *
     *  Unified utility for creating push notification packets
     *  that work on BOTH protocol lanes (8-bit and 16-bit).
     *  
     *  Eliminates code duplication and prevents opcode bugs
     *  by providing lane-aware packet building with compile-time
     *  type safety.
     **/
    class PushNotificationBuilder
    {
    public:
        /** BuildChannelNotification
         *
         *  Create a channel-specific push notification packet.
         *  Template specialization ensures correct packet type for lane.
         *  
         *  Packet format (12 bytes payload):
         *    [0-3]   unified_height (uint32_t, big-endian)
         *    [4-7]   channel_height (uint32_t, big-endian)
         *    [8-11]  difficulty (uint32_t, big-endian)
         *
         *  @param[in] nChannel Mining channel (1=Prime, 2=Hash)
         *  @param[in] lane Protocol lane (LEGACY or STATELESS)
         *  @param[in] stateBest Current blockchain state
         *  @param[in] stateChannel Channel-specific state
         *  @param[in] nDifficulty Mining difficulty
         *
         *  @return Packet (8-bit) or StatelessPacket (16-bit)
         *
         *  Opcode selection:
         *    Channel 1 (Prime) + LEGACY    → 0xD9 (PRIME_BLOCK_AVAILABLE)
         *    Channel 1 (Prime) + STATELESS → 0xD0D9 (STATELESS_PRIME_BLOCK_AVAILABLE)
         *    Channel 2 (Hash) + LEGACY     → 0xDA (HASH_BLOCK_AVAILABLE)
         *    Channel 2 (Hash) + STATELESS  → 0xD0DA (STATELESS_HASH_BLOCK_AVAILABLE)
         *  
         *  Example usage:
         *    // Legacy lane (8-bit):
         *    auto notif = BuildChannelNotification<Packet>(1, LEGACY, ...);
         *    
         *    // Stateless lane (16-bit):
         *    auto notif = BuildChannelNotification<StatelessPacket>(1, STATELESS, ...);
         **/
        template<typename PacketType>
        static PacketType BuildChannelNotification(
            uint32_t nChannel,
            ProtocolLane lane,
            const TAO::Ledger::BlockState& stateBest,
            const TAO::Ledger::BlockState& stateChannel,
            uint32_t nDifficulty
        );

        /** BuildStatelessTemplate
         *
         *  Create a complete mining template push packet (stateless-only).
         *  
         *  Packet format (228 bytes payload):
         *    [0-3]    unified_height (uint32_t, big-endian)
         *    [4-7]    channel_height (uint32_t, big-endian)
         *    [8-11]   difficulty (uint32_t, big-endian)
         *    [12-227] block_template (216 bytes, Tritium serialized)
         *
         *  @param[in] stateBest Current blockchain state
         *  @param[in] stateChannel Channel-specific state
         *  @param[in] nDifficulty Mining difficulty
         *  @param[in] pBlock Pointer to block template
         *
         *  @return StatelessPacket with opcode 0xD081 (STATELESS_GET_BLOCK)
         *
         *  Note: Legacy lane doesn't support template push (only notification).
         *        Miners on legacy lane receive PRIME/HASH_BLOCK_AVAILABLE then
         *        request template via GET_BLOCK.
         **/
        static StatelessPacket BuildStatelessTemplate(
            const TAO::Ledger::BlockState& stateBest,
            const TAO::Ledger::BlockState& stateChannel,
            uint32_t nDifficulty,
            const TAO::Ledger::Block* pBlock
        );

    private:
        /** GetNotificationOpcode
         *
         *  Select correct push notification opcode based on channel and lane.
         *  Prevents opcode bugs by centralizing selection logic.
         *
         *  @param[in] nChannel Mining channel (1=Prime, 2=Hash)
         *  @param[in] lane Protocol lane (LEGACY or STATELESS)
         *
         *  @return 8-bit opcode (LEGACY) or 16-bit opcode (STATELESS)
         *  
         *  @throws std::invalid_argument if channel is invalid
         **/
        static uint16_t GetNotificationOpcode(uint32_t nChannel, ProtocolLane lane);

        /** BuildPayload
         *
         *  Build 12-byte push notification payload (big-endian).
         *  Used by both channel notifications and template pushes.
         *
         *  @param[in] stateBest Current blockchain state
         *  @param[in] stateChannel Channel-specific state
         *  @param[in] nDifficulty Mining difficulty
         *
         *  @return 12-byte payload vector
         **/
        static std::vector<uint8_t> BuildPayload(
            const TAO::Ledger::BlockState& stateBest,
            const TAO::Ledger::BlockState& stateChannel,
            uint32_t nDifficulty
        );
    };

} // namespace LLP

#endif

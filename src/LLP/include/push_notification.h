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

namespace TAO::Ledger
{
    class BlockState;
    class Block;
}

namespace LLP
{
    /** ProtocolLane - Determines opcode format (8-bit vs 16-bit) **/
    enum class ProtocolLane : uint8_t
    {
        LEGACY = 0,      // Port 8323, 8-bit opcodes
        STATELESS = 1    // Port 9323+, 16-bit opcodes
    };

    /** PushNotificationBuilder - Unified push notification utility
     *
     *  Provides a centralized implementation for building push notifications
     *  that work on both legacy (8-bit) and stateless (16-bit) protocol lanes.
     *
     *  This eliminates code duplication and ensures correct opcode selection
     *  based on the protocol lane.
     *
     **/
    class PushNotificationBuilder
    {
    public:
        /** BuildChannelNotification
         *
         *  Create channel-specific push notification (12-byte payload).
         *  
         *  Template specializations:
         *  - Packet: For legacy lane (8-bit opcodes)
         *  - StatelessPacket: For stateless lane (16-bit opcodes)
         *
         *  Opcode selection:
         *  - Channel 1 (Prime) + LEGACY    → 0xD9 (PRIME_BLOCK_AVAILABLE)
         *  - Channel 1 (Prime) + STATELESS → 0xD0D9 (STATELESS_PRIME_BLOCK_AVAILABLE)
         *  - Channel 2 (Hash) + LEGACY     → 0xDA (HASH_BLOCK_AVAILABLE)
         *  - Channel 2 (Hash) + STATELESS  → 0xD0DA (STATELESS_HASH_BLOCK_AVAILABLE)
         *
         *  Payload format (12 bytes, big-endian):
         *  [0-3]   unified_height (uint32_t)
         *  [4-7]   channel_height (uint32_t)
         *  [8-11]  difficulty (uint32_t)
         *
         *  @param[in] nChannel The mining channel (1=Prime, 2=Hash)
         *  @param[in] lane The protocol lane (LEGACY or STATELESS)
         *  @param[in] stateBest The current best blockchain state
         *  @param[in] stateChannel The channel-specific blockchain state
         *  @param[in] nDifficulty The current mining difficulty
         *
         *  @return A packet (Packet or StatelessPacket) ready to send
         *
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
         *  Create complete mining template push (228-byte payload).
         *  Stateless-only feature (16-bit opcode 0xD081).
         *
         *  Payload format (228 bytes):
         *  [0-3]    unified_height (uint32_t, big-endian)
         *  [4-7]    channel_height (uint32_t, big-endian)
         *  [8-11]   difficulty (uint32_t, big-endian)
         *  [12-227] block_template (216 bytes, Tritium serialized)
         *
         *  @param[in] stateBest The current best blockchain state
         *  @param[in] stateChannel The channel-specific blockchain state
         *  @param[in] nDifficulty The current mining difficulty
         *  @param[in] pBlock Pointer to the block template to send
         *
         *  @return A StatelessPacket ready to send
         *
         **/
        static StatelessPacket BuildStatelessTemplate(
            const TAO::Ledger::BlockState& stateBest,
            const TAO::Ledger::BlockState& stateChannel,
            uint32_t nDifficulty,
            const TAO::Ledger::Block* pBlock
        );

    private:
        /** GetNotificationOpcode - Lane-aware opcode selection
         *
         *  Selects the appropriate opcode based on channel and protocol lane.
         *
         *  @param[in] nChannel The mining channel (1=Prime, 2=Hash)
         *  @param[in] lane The protocol lane (LEGACY or STATELESS)
         *
         *  @return The appropriate opcode (8-bit or 16-bit)
         *
         **/
        static uint16_t GetNotificationOpcode(uint32_t nChannel, ProtocolLane lane);

        /** BuildPayload - Create 12-byte metadata payload (big-endian)
         *
         *  Creates the common 12-byte payload used by both channel notifications
         *  and stateless templates.
         *
         *  @param[in] stateBest The current best blockchain state
         *  @param[in] stateChannel The channel-specific blockchain state
         *  @param[in] nDifficulty The current mining difficulty
         *
         *  @return Vector containing the 12-byte payload
         *
         **/
        static std::vector<uint8_t> BuildPayload(
            const TAO::Ledger::BlockState& stateBest,
            const TAO::Ledger::BlockState& stateChannel,
            uint32_t nDifficulty
        );
    };

} // namespace LLP

#endif

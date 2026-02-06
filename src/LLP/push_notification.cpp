/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLP/include/push_notification.h>
#include <LLP/include/opcode_utility.h>
#include <LLP/packets/packet.h>
#include <LLP/packets/stateless_packet.h>
#include <TAO/Ledger/types/state.h>
#include <TAO/Ledger/types/block.h>
#include <Util/include/debug.h>

#include <stdexcept>

namespace LLP
{
    using namespace OpcodeUtility;

    /* Helper function: Get human-readable channel name */
    namespace {
        const char* GetChannelName(uint32_t nChannel, bool fLegacy = false)
        {
            if (fLegacy)
            {
                return (nChannel == 1) ? "PRIME_BLOCK_AVAILABLE" : "HASH_BLOCK_AVAILABLE";
            }
            else
            {
                return (nChannel == 1) ? "STATELESS_PRIME_BLOCK_AVAILABLE" : "STATELESS_HASH_BLOCK_AVAILABLE";
            }
        }
    }

    /* GetNotificationOpcode - Lane-aware opcode selection */
    uint16_t PushNotificationBuilder::GetNotificationOpcode(uint32_t nChannel, ProtocolLane lane)
    {
        if (lane == ProtocolLane::LEGACY)
        {
            // 8-bit opcodes for legacy lane
            if (nChannel == 1)
                return Opcodes::PRIME_BLOCK_AVAILABLE;  // 0xD9 (217)
            else if (nChannel == 2)
                return Opcodes::HASH_BLOCK_AVAILABLE;   // 0xDA (218)
            else
                throw std::invalid_argument("Invalid channel for push notification");
        }
        else // ProtocolLane::STATELESS
        {
            // 16-bit mirror-mapped opcodes for stateless lane
            if (nChannel == 1)
                return Stateless::PRIME_BLOCK_AVAILABLE;  // 0xD0D9
            else if (nChannel == 2)
                return Stateless::HASH_BLOCK_AVAILABLE;   // 0xD0DA
            else
                throw std::invalid_argument("Invalid channel for push notification");
        }
    }

    /* BuildPayload - Create 12-byte big-endian payload */
    std::vector<uint8_t> PushNotificationBuilder::BuildPayload(
        const TAO::Ledger::BlockState& stateBest,
        const TAO::Ledger::BlockState& stateChannel,
        uint32_t nDifficulty
    )
    {
        std::vector<uint8_t> payload;
        payload.reserve(12);

        // Unified height [0-3] (big-endian)
        payload.push_back((stateBest.nHeight >> 24) & 0xFF);
        payload.push_back((stateBest.nHeight >> 16) & 0xFF);
        payload.push_back((stateBest.nHeight >> 8) & 0xFF);
        payload.push_back((stateBest.nHeight >> 0) & 0xFF);

        // Channel height [4-7] (big-endian)
        uint32_t nChannelHeight = stateChannel.nChannelHeight;
        payload.push_back((nChannelHeight >> 24) & 0xFF);
        payload.push_back((nChannelHeight >> 16) & 0xFF);
        payload.push_back((nChannelHeight >> 8) & 0xFF);
        payload.push_back((nChannelHeight >> 0) & 0xFF);

        // Difficulty [8-11] (big-endian)
        payload.push_back((nDifficulty >> 24) & 0xFF);
        payload.push_back((nDifficulty >> 16) & 0xFF);
        payload.push_back((nDifficulty >> 8) & 0xFF);
        payload.push_back((nDifficulty >> 0) & 0xFF);

        return payload;
    }

    /* BuildChannelNotification - Template specialization for 8-bit */
    template<>
    Packet PushNotificationBuilder::BuildChannelNotification<Packet>(
        uint32_t nChannel,
        ProtocolLane lane,
        const TAO::Ledger::BlockState& stateBest,
        const TAO::Ledger::BlockState& stateChannel,
        uint32_t nDifficulty
    )
    {
        // Get 8-bit opcode (lane must be LEGACY)
        if (lane != ProtocolLane::LEGACY)
            throw std::invalid_argument("Packet template requires LEGACY lane");

        uint8_t nOpcode = static_cast<uint8_t>(GetNotificationOpcode(nChannel, lane));
        std::vector<uint8_t> payload = BuildPayload(stateBest, stateChannel, nDifficulty);

        Packet packet;
        packet.HEADER = nOpcode;
        packet.LENGTH = 12;
        packet.DATA = payload;

        debug::log(2, "[PushNotification] Built LEGACY notification:");
        debug::log(2, "   Opcode: 0x", std::hex, (uint32_t)nOpcode, std::dec, 
            " (", GetChannelName(nChannel, true), ")");
        debug::log(2, "   Lane: 8-bit (LEGACY)");
        debug::log(2, "   Payload: 12 bytes");

        return packet;
    }

    /* BuildChannelNotification - Template specialization for 16-bit */
    template<>
    StatelessPacket PushNotificationBuilder::BuildChannelNotification<StatelessPacket>(
        uint32_t nChannel,
        ProtocolLane lane,
        const TAO::Ledger::BlockState& stateBest,
        const TAO::Ledger::BlockState& stateChannel,
        uint32_t nDifficulty
    )
    {
        // Get 16-bit opcode (lane must be STATELESS)
        if (lane != ProtocolLane::STATELESS)
            throw std::invalid_argument("StatelessPacket template requires STATELESS lane");

        uint16_t nOpcode = GetNotificationOpcode(nChannel, lane);
        std::vector<uint8_t> payload = BuildPayload(stateBest, stateChannel, nDifficulty);

        StatelessPacket packet(nOpcode);
        packet.DATA = payload;
        packet.LENGTH = 12;

        debug::log(2, "[PushNotification] Built STATELESS notification:");
        debug::log(2, "   Opcode: 0x", std::hex, nOpcode, std::dec,
            " (", GetChannelName(nChannel, false), ")");
        debug::log(2, "   Lane: 16-bit (STATELESS)");
        debug::log(2, "   Payload: 12 bytes");

        return packet;
    }

    /* BuildStatelessTemplate - Stateless-only feature */
    StatelessPacket PushNotificationBuilder::BuildStatelessTemplate(
        const TAO::Ledger::BlockState& stateBest,
        const TAO::Ledger::BlockState& stateChannel,
        uint32_t nDifficulty,
        const TAO::Ledger::Block* pBlock
    )
    {
        if (!pBlock)
            throw std::invalid_argument("Block template is nullptr");

        // Expected block size for Tritium (matches TRITIUM_BLOCK_SIZE constant in stateless_miner_connection.cpp)
        static constexpr size_t EXPECTED_BLOCK_SIZE = 216;

        // Serialize block (expected 216 bytes for Tritium)
        std::vector<uint8_t> vBlockData = pBlock->Serialize();
        if (vBlockData.size() != EXPECTED_BLOCK_SIZE)
        {
            debug::error("[PushNotification] Invalid block size: ", vBlockData.size(), " bytes (expected ", EXPECTED_BLOCK_SIZE, ")");
            throw std::runtime_error("Invalid block template size");
        }

        // Build packet with opcode 0xD081 (STATELESS_GET_BLOCK)
        StatelessPacket packet(Stateless::GET_BLOCK);  // 0xD081

        // Add 12-byte metadata
        std::vector<uint8_t> metadata = BuildPayload(stateBest, stateChannel, nDifficulty);
        packet.DATA = metadata;

        // Add 216-byte block template
        packet.DATA.insert(packet.DATA.end(), vBlockData.begin(), vBlockData.end());
        packet.LENGTH = static_cast<uint32_t>(packet.DATA.size());  // 228 bytes

        debug::log(2, "[PushNotification] Built stateless template:");
        debug::log(2, "   Opcode: 0xD081 (STATELESS_GET_BLOCK)");
        debug::log(2, "   Total size: ", packet.LENGTH, " bytes (12 meta + 216 template)");

        return packet;
    }

} // namespace LLP

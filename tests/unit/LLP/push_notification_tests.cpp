/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/push_notification.h>
#include <LLP/include/opcode_utility.h>
#include <LLP/packets/packet.h>
#include <LLP/packets/stateless_packet.h>
#include <TAO/Ledger/types/state.h>
#include <TAO/Ledger/types/block.h>

#include <vector>
#include <cstdint>

/* Test unified push notification builder
 * 
 * The PushNotificationBuilder must:
 * - Select correct opcodes based on lane (LEGACY vs STATELESS)
 * - Build proper 12-byte payload format (big-endian)
 * - Support both Packet (8-bit) and StatelessPacket (16-bit)
 * - Prevent lane mismatch bugs via template specialization
 */

TEST_CASE("Push Notification Builder Tests", "[push_notification][llp]")
{
    using namespace LLP;
    using namespace OpcodeUtility;

    /* Create mock blockchain states for testing */
    TAO::Ledger::BlockState stateBest;
    stateBest.nHeight = 1000;
    stateBest.nChannelHeight = 500;
    
    TAO::Ledger::BlockState statePrimeChannel;
    statePrimeChannel.nHeight = 1000;
    statePrimeChannel.nChannelHeight = 500;
    
    TAO::Ledger::BlockState stateHashChannel;
    stateHashChannel.nHeight = 1000;
    stateHashChannel.nChannelHeight = 600;
    
    uint32_t nDifficulty = 0x12345678;

    SECTION("BuildChannelNotification - Legacy Lane - Prime Channel")
    {
        Packet notification = PushNotificationBuilder::BuildChannelNotification<Packet>(
            1,  // Prime channel
            ProtocolLane::LEGACY,
            stateBest,
            statePrimeChannel,
            nDifficulty
        );

        /* Verify opcode is 8-bit PRIME_BLOCK_AVAILABLE */
        REQUIRE(notification.HEADER == Opcodes::PRIME_BLOCK_AVAILABLE);
        REQUIRE(notification.HEADER == 0xD9);
        REQUIRE(notification.HEADER == 217);

        /* Verify payload is 12 bytes */
        REQUIRE(notification.LENGTH == 12);
        REQUIRE(notification.DATA.size() == 12);

        /* Verify payload structure (big-endian) */
        // Unified height [0-3]
        REQUIRE(notification.DATA[0] == 0x00);
        REQUIRE(notification.DATA[1] == 0x00);
        REQUIRE(notification.DATA[2] == 0x03);
        REQUIRE(notification.DATA[3] == 0xE8);  // 1000 in big-endian
        
        // Channel height [4-7]
        REQUIRE(notification.DATA[4] == 0x00);
        REQUIRE(notification.DATA[5] == 0x00);
        REQUIRE(notification.DATA[6] == 0x01);
        REQUIRE(notification.DATA[7] == 0xF4);  // 500 in big-endian
        
        // Difficulty [8-11]
        REQUIRE(notification.DATA[8] == 0x12);
        REQUIRE(notification.DATA[9] == 0x34);
        REQUIRE(notification.DATA[10] == 0x56);
        REQUIRE(notification.DATA[11] == 0x78);
    }

    SECTION("BuildChannelNotification - Legacy Lane - Hash Channel")
    {
        Packet notification = PushNotificationBuilder::BuildChannelNotification<Packet>(
            2,  // Hash channel
            ProtocolLane::LEGACY,
            stateBest,
            stateHashChannel,
            nDifficulty
        );

        /* Verify opcode is 8-bit HASH_BLOCK_AVAILABLE */
        REQUIRE(notification.HEADER == Opcodes::HASH_BLOCK_AVAILABLE);
        REQUIRE(notification.HEADER == 0xDA);
        REQUIRE(notification.HEADER == 218);

        /* Verify payload is 12 bytes */
        REQUIRE(notification.LENGTH == 12);
        REQUIRE(notification.DATA.size() == 12);
        
        // Channel height should be 600 for Hash channel
        REQUIRE(notification.DATA[4] == 0x00);
        REQUIRE(notification.DATA[5] == 0x00);
        REQUIRE(notification.DATA[6] == 0x02);
        REQUIRE(notification.DATA[7] == 0x58);  // 600 in big-endian
    }

    SECTION("BuildChannelNotification - Stateless Lane - Prime Channel")
    {
        StatelessPacket notification = PushNotificationBuilder::BuildChannelNotification<StatelessPacket>(
            1,  // Prime channel
            ProtocolLane::STATELESS,
            stateBest,
            statePrimeChannel,
            nDifficulty
        );

        /* Verify opcode is 16-bit STATELESS_PRIME_BLOCK_AVAILABLE */
        REQUIRE(notification.HEADER == Stateless::PRIME_BLOCK_AVAILABLE);
        REQUIRE(notification.HEADER == 0xD0D9);

        /* Verify payload is 12 bytes */
        REQUIRE(notification.LENGTH == 12);
        REQUIRE(notification.DATA.size() == 12);
    }

    SECTION("BuildChannelNotification - Stateless Lane - Hash Channel")
    {
        StatelessPacket notification = PushNotificationBuilder::BuildChannelNotification<StatelessPacket>(
            2,  // Hash channel
            ProtocolLane::STATELESS,
            stateBest,
            stateHashChannel,
            nDifficulty
        );

        /* Verify opcode is 16-bit STATELESS_HASH_BLOCK_AVAILABLE */
        REQUIRE(notification.HEADER == Stateless::HASH_BLOCK_AVAILABLE);
        REQUIRE(notification.HEADER == 0xD0DA);

        /* Verify payload is 12 bytes */
        REQUIRE(notification.LENGTH == 12);
        REQUIRE(notification.DATA.size() == 12);
    }

    SECTION("BuildChannelNotification - Invalid Channel")
    {
        /* Channel 0 (Stake) is not valid for mining push notifications */
        REQUIRE_THROWS_AS(
            PushNotificationBuilder::BuildChannelNotification<Packet>(
                0,  // Invalid channel
                ProtocolLane::LEGACY,
                stateBest,
                statePrimeChannel,
                nDifficulty
            ),
            std::invalid_argument
        );
        
        /* Channel 3 doesn't exist */
        REQUIRE_THROWS_AS(
            PushNotificationBuilder::BuildChannelNotification<Packet>(
                3,  // Invalid channel
                ProtocolLane::LEGACY,
                stateBest,
                statePrimeChannel,
                nDifficulty
            ),
            std::invalid_argument
        );
    }

    SECTION("BuildChannelNotification - Lane Mismatch Prevention")
    {
        /* Using Packet (8-bit) with STATELESS lane should throw */
        REQUIRE_THROWS_AS(
            PushNotificationBuilder::BuildChannelNotification<Packet>(
                1,
                ProtocolLane::STATELESS,  // Wrong lane for Packet type
                stateBest,
                statePrimeChannel,
                nDifficulty
            ),
            std::invalid_argument
        );
        
        /* Using StatelessPacket (16-bit) with LEGACY lane should throw */
        REQUIRE_THROWS_AS(
            PushNotificationBuilder::BuildChannelNotification<StatelessPacket>(
                1,
                ProtocolLane::LEGACY,  // Wrong lane for StatelessPacket type
                stateBest,
                statePrimeChannel,
                nDifficulty
            ),
            std::invalid_argument
        );
    }

    SECTION("Opcode Mirror Mapping Verification")
    {
        /* Verify that stateless opcodes are correctly mirror-mapped */
        REQUIRE(Stateless::PRIME_BLOCK_AVAILABLE == Stateless::Mirror(Opcodes::PRIME_BLOCK_AVAILABLE));
        REQUIRE(Stateless::HASH_BLOCK_AVAILABLE == Stateless::Mirror(Opcodes::HASH_BLOCK_AVAILABLE));
        
        /* Verify the mirror formula: stateless = 0xD000 | legacy */
        REQUIRE(Stateless::PRIME_BLOCK_AVAILABLE == (0xD000 | Opcodes::PRIME_BLOCK_AVAILABLE));
        REQUIRE(Stateless::HASH_BLOCK_AVAILABLE == (0xD000 | Opcodes::HASH_BLOCK_AVAILABLE));
    }
}

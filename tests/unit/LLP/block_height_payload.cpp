/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/opcode_utility.h>
#include <LLP/include/stateless_manager.h>
#include <LLP/packets/packet.h>
#include <LLP/packets/stateless_packet.h>

namespace
{
    uint32_t ReadBigEndian32(const std::vector<uint8_t>& vBytes, const int nOffset)
    {
        return (static_cast<uint32_t>(vBytes.at(nOffset + 0)) << 24) |
               (static_cast<uint32_t>(vBytes.at(nOffset + 1)) << 16) |
               (static_cast<uint32_t>(vBytes.at(nOffset + 2)) << 8)  |
               static_cast<uint32_t>(vBytes.at(nOffset + 3));
    }
}


TEST_CASE("BLOCK_HEIGHT payload serializes full height snapshot", "[llp][get_height][block_height]")
{
    const LLP::StatelessMinerManager::GetHeightSnapshot snapshot{
        0x11223344u,
        0x55667788u,
        0x99AABBCCu,
        0xDDEEFF00u,
        0x12345678u
    };

    const std::vector<uint8_t> vPayload =
        LLP::StatelessMinerManager::BuildBlockHeightPayload(snapshot);

    REQUIRE(vPayload.size() == 16);

    const std::vector<uint8_t> vExpected{
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC,
        0xDD, 0xEE, 0xFF, 0x00
    };

    REQUIRE(vPayload == vExpected);
    REQUIRE(ReadBigEndian32(vPayload, 0) == snapshot.nUnifiedHeight);
    REQUIRE(ReadBigEndian32(vPayload, 4) == snapshot.nPrimeHeight);
    REQUIRE(ReadBigEndian32(vPayload, 8) == snapshot.nHashHeight);
    REQUIRE(ReadBigEndian32(vPayload, 12) == snapshot.nStakeHeight);
}


TEST_CASE("BLOCK_HEIGHT payload parses back to the same snapshot", "[llp][get_height][block_height]")
{
    const LLP::StatelessMinerManager::GetHeightSnapshot snapshot{
        6543210u,
        2302709u,
        2166317u,
        2068487u,
        0u
    };

    const std::vector<uint8_t> vPayload =
        LLP::StatelessMinerManager::BuildBlockHeightPayload(snapshot);

    LLP::StatelessMinerManager::GetHeightSnapshot parsed;
    REQUIRE(LLP::StatelessMinerManager::ParseBlockHeightPayload(vPayload, parsed));

    REQUIRE(parsed.nUnifiedHeight == snapshot.nUnifiedHeight);
    REQUIRE(parsed.nPrimeHeight == snapshot.nPrimeHeight);
    REQUIRE(parsed.nHashHeight == snapshot.nHashHeight);
    REQUIRE(parsed.nStakeHeight == snapshot.nStakeHeight);

    const std::vector<uint8_t> vLegacyPayload{
        vPayload.begin(),
        vPayload.begin() + 4
    };
    REQUIRE_FALSE(LLP::StatelessMinerManager::ParseBlockHeightPayload(vLegacyPayload, parsed));
}


TEST_CASE("BLOCK_HEIGHT packet framing reports 16-byte payload on both lanes", "[llp][get_height][block_height]")
{
    const LLP::StatelessMinerManager::GetHeightSnapshot snapshot{
        100u, 90u, 80u, 70u, 0u
    };

    const std::vector<uint8_t> vPayload =
        LLP::StatelessMinerManager::BuildBlockHeightPayload(snapshot);

    SECTION("Legacy BLOCK_HEIGHT packet uses opcode label and 16-byte length field")
    {
        LLP::Packet packet;
        packet.HEADER = LLP::OpcodeUtility::Opcodes::BLOCK_HEIGHT;
        packet.DATA = vPayload;
        packet.LENGTH = static_cast<uint32_t>(packet.DATA.size());

        const std::vector<uint8_t> vBytes = packet.GetBytes();

        REQUIRE(LLP::OpcodeUtility::GetOpcodeName(packet.HEADER) == "BLOCK_HEIGHT");
        REQUIRE(vBytes.size() == 1 + 4 + 16);
        REQUIRE(vBytes[0] == LLP::OpcodeUtility::Opcodes::BLOCK_HEIGHT);
        REQUIRE(ReadBigEndian32(vBytes, 1) == 16u);
    }

    SECTION("Stateless BLOCK_HEIGHT packet uses mirrored opcode label and 16-byte length field")
    {
        LLP::StatelessPacket packet(LLP::OpcodeUtility::Stateless::BLOCK_HEIGHT);
        packet.DATA = vPayload;
        packet.LENGTH = static_cast<uint32_t>(packet.DATA.size());

        const std::vector<uint8_t> vBytes = packet.GetBytes();

        REQUIRE(LLP::OpcodeUtility::GetOpcodeName16(packet.HEADER) == "STATELESS_BLOCK_HEIGHT");
        REQUIRE(vBytes.size() == 2 + 4 + 16);
        REQUIRE(vBytes[0] == 0xD0);
        REQUIRE(vBytes[1] == LLP::OpcodeUtility::Opcodes::BLOCK_HEIGHT);
        REQUIRE(ReadBigEndian32(vBytes, 2) == 16u);
    }
}

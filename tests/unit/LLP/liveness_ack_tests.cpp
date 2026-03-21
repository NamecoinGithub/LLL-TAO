/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/liveness_ack.h>
#include <LLP/include/stateless_miner.h>

#include <cstdint>
#include <vector>

using namespace LLP;
using namespace LLP::LivenessAck;

/*
 * Unit tests for liveness_ack.h — centralized session-liveness ACK helpers.
 *
 * Covers:
 *   - BuildSessionExpiredPayload: wire format, reason codes, session ID encoding
 *   - ClassifyPacket: correct AckPacketFamily for all known liveness opcodes
 *   - AckFamilyName: non-null string for every AckPacketFamily value
 *   - StatelessMiner::ApplyKeepaliveHealth: canonical context update correctness
 */

TEST_CASE("LivenessAck::BuildSessionExpiredPayload", "[liveness_ack][llp]")
{
    SECTION("Default reason is EXPIRED_REASON_INACTIVITY (0x01)")
    {
        auto payload = BuildSessionExpiredPayload(0x12345678u);

        REQUIRE(payload.size() == 5u);
        /* session_id little-endian */
        REQUIRE(payload[0] == 0x78u);
        REQUIRE(payload[1] == 0x56u);
        REQUIRE(payload[2] == 0x34u);
        REQUIRE(payload[3] == 0x12u);
        /* reason */
        REQUIRE(payload[4] == EXPIRED_REASON_INACTIVITY);
    }

    SECTION("Explicit EXPIRED_REASON_AUTH_LOST (0x02)")
    {
        auto payload = BuildSessionExpiredPayload(0xDEADBEEFu, EXPIRED_REASON_AUTH_LOST);

        REQUIRE(payload.size() == 5u);
        REQUIRE(payload[0] == 0xEFu);
        REQUIRE(payload[1] == 0xBEu);
        REQUIRE(payload[2] == 0xADu);
        REQUIRE(payload[3] == 0xDEu);
        REQUIRE(payload[4] == EXPIRED_REASON_AUTH_LOST);
    }

    SECTION("Session ID zero produces all-zero session bytes")
    {
        auto payload = BuildSessionExpiredPayload(0u);

        REQUIRE(payload.size() == 5u);
        REQUIRE(payload[0] == 0u);
        REQUIRE(payload[1] == 0u);
        REQUIRE(payload[2] == 0u);
        REQUIRE(payload[3] == 0u);
        REQUIRE(payload[4] == EXPIRED_REASON_INACTIVITY);
    }

    SECTION("Session ID 0xFFFFFFFF produces all-0xFF session bytes")
    {
        auto payload = BuildSessionExpiredPayload(0xFFFFFFFFu);

        REQUIRE(payload.size() == 5u);
        REQUIRE(payload[0] == 0xFFu);
        REQUIRE(payload[1] == 0xFFu);
        REQUIRE(payload[2] == 0xFFu);
        REQUIRE(payload[3] == 0xFFu);
        REQUIRE(payload[4] == EXPIRED_REASON_INACTIVITY);
    }

    SECTION("Same session ID, different reason codes produce different payloads")
    {
        auto p1 = BuildSessionExpiredPayload(1u, EXPIRED_REASON_INACTIVITY);
        auto p2 = BuildSessionExpiredPayload(1u, EXPIRED_REASON_AUTH_LOST);

        REQUIRE(p1 != p2);
        REQUIRE(p1[4] != p2[4]);
    }

    SECTION("Round-trip: session_id recoverable from payload bytes (little-endian)")
    {
        uint32_t original = 0xA1B2C3D4u;
        auto payload = BuildSessionExpiredPayload(original);

        uint32_t recovered =
            static_cast<uint32_t>(payload[0])
          | (static_cast<uint32_t>(payload[1]) <<  8)
          | (static_cast<uint32_t>(payload[2]) << 16)
          | (static_cast<uint32_t>(payload[3]) << 24);

        REQUIRE(recovered == original);
    }
}


TEST_CASE("LivenessAck::ClassifyPacket", "[liveness_ack][llp]")
{
    SECTION("SESSION_KEEPALIVE — legacy and stateless mirror both map to SESSION_KEEPALIVE")
    {
        REQUIRE(ClassifyPacket(212u)    == AckPacketFamily::SESSION_KEEPALIVE);  // 0xD4
        REQUIRE(ClassifyPacket(0xD0D4u) == AckPacketFamily::SESSION_KEEPALIVE);
    }

    SECTION("KEEPALIVE_V2 and KEEPALIVE_V2_ACK")
    {
        REQUIRE(ClassifyPacket(0xD100u) == AckPacketFamily::KEEPALIVE_V2);
        REQUIRE(ClassifyPacket(0xD101u) == AckPacketFamily::KEEPALIVE_V2_ACK);
    }

    SECTION("SESSION_STATUS — legacy and stateless mirror both map to SESSION_STATUS")
    {
        REQUIRE(ClassifyPacket(219u)    == AckPacketFamily::SESSION_STATUS);  // 0xDB
        REQUIRE(ClassifyPacket(0xD0DBu) == AckPacketFamily::SESSION_STATUS);
    }

    SECTION("SESSION_STATUS_ACK — legacy and stateless mirror both map to SESSION_STATUS_ACK")
    {
        REQUIRE(ClassifyPacket(220u)    == AckPacketFamily::SESSION_STATUS_ACK);  // 0xDC
        REQUIRE(ClassifyPacket(0xD0DCu) == AckPacketFamily::SESSION_STATUS_ACK);
    }

    SECTION("SESSION_EXPIRED — legacy and stateless mirror both map to SESSION_EXPIRED")
    {
        REQUIRE(ClassifyPacket(221u)    == AckPacketFamily::SESSION_EXPIRED);  // 0xDD
        REQUIRE(ClassifyPacket(0xD0DDu) == AckPacketFamily::SESSION_EXPIRED);
    }

    SECTION("Non-liveness opcodes return UNKNOWN")
    {
        REQUIRE(ClassifyPacket(0u)      == AckPacketFamily::UNKNOWN);
        REQUIRE(ClassifyPacket(1u)      == AckPacketFamily::UNKNOWN);  // SUBMIT_BLOCK
        REQUIRE(ClassifyPacket(129u)    == AckPacketFamily::UNKNOWN);  // GET_BLOCK
        REQUIRE(ClassifyPacket(0xFFFFu) == AckPacketFamily::UNKNOWN);
    }

    SECTION("All known liveness opcodes are non-UNKNOWN")
    {
        /* Exhaustive check that no liveness opcode falls through to UNKNOWN */
        const std::vector<uint16_t> livenessOpcodes = {
            212u, 0xD0D4u,
            0xD100u, 0xD101u,
            219u, 0xD0DBu,
            220u, 0xD0DCu,
            221u, 0xD0DDu
        };
        for(auto op : livenessOpcodes)
            REQUIRE(ClassifyPacket(op) != AckPacketFamily::UNKNOWN);
    }
}


TEST_CASE("LivenessAck::AckFamilyName", "[liveness_ack][llp]")
{
    SECTION("Every AckPacketFamily value returns a non-empty string")
    {
        const std::vector<AckPacketFamily> families = {
            AckPacketFamily::SESSION_KEEPALIVE,
            AckPacketFamily::KEEPALIVE_V2,
            AckPacketFamily::KEEPALIVE_V2_ACK,
            AckPacketFamily::SESSION_STATUS,
            AckPacketFamily::SESSION_STATUS_ACK,
            AckPacketFamily::SESSION_EXPIRED,
            AckPacketFamily::UNKNOWN
        };
        for(auto f : families)
        {
            const char* name = AckFamilyName(f);
            REQUIRE(name != nullptr);
            REQUIRE(name[0] != '\0');
        }
    }

    SECTION("Known families return expected names")
    {
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::SESSION_KEEPALIVE))  == "SESSION_KEEPALIVE");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::KEEPALIVE_V2))       == "KEEPALIVE_V2");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::KEEPALIVE_V2_ACK))   == "KEEPALIVE_V2_ACK");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::SESSION_STATUS))     == "SESSION_STATUS");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::SESSION_STATUS_ACK)) == "SESSION_STATUS_ACK");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::SESSION_EXPIRED))    == "SESSION_EXPIRED");
        REQUIRE(std::string(AckFamilyName(AckPacketFamily::UNKNOWN))            == "UNKNOWN");
    }
}


TEST_CASE("StatelessMiner::ApplyKeepaliveHealth", "[liveness_ack][llp]")
{
    SECTION("All four liveness fields updated in one call (no stake update)")
    {
        MiningContext ctx;
        ctx.nKeepaliveCount    = 5u;
        ctx.nKeepaliveSent     = 3u;
        ctx.nLastKeepaliveTime = 1000u;
        ctx.nTimestamp         = 900u;

        const uint64_t nNow = 2000u;
        MiningContext updated = StatelessMiner::ApplyKeepaliveHealth(ctx, nNow);

        REQUIRE(updated.nTimestamp         == nNow);
        REQUIRE(updated.nKeepaliveCount    == 6u);
        REQUIRE(updated.nKeepaliveSent     == 4u);
        REQUIRE(updated.nLastKeepaliveTime == nNow);
    }

    SECTION("Stake height persisted when fUpdateStake is true")
    {
        MiningContext ctx;
        ctx.nKeepaliveCount = 0u;
        ctx.nKeepaliveSent  = 0u;

        MiningContext updated = StatelessMiner::ApplyKeepaliveHealth(ctx, 5000u, 999u, true);

        REQUIRE(updated.nKeepaliveCount == 1u);
        REQUIRE(updated.nKeepaliveSent  == 1u);
        REQUIRE(updated.nStakeHeight    == 999u);
    }

    SECTION("Stake height NOT changed when fUpdateStake is false")
    {
        MiningContext ctx;
        ctx.nStakeHeight = 42u;

        MiningContext updated = StatelessMiner::ApplyKeepaliveHealth(ctx, 5000u, 999u, false);

        /* nStakeHeight must remain unchanged since fUpdateStake=false */
        REQUIRE(updated.nStakeHeight == 42u);
    }

    SECTION("Counters increment correctly across multiple calls")
    {
        MiningContext ctx;
        ctx.nKeepaliveCount = 0u;
        ctx.nKeepaliveSent  = 0u;

        for(uint32_t i = 1; i <= 5; ++i)
        {
            uint64_t nNow = static_cast<uint64_t>(i * 1000);
            ctx = StatelessMiner::ApplyKeepaliveHealth(ctx, nNow);
            REQUIRE(ctx.nKeepaliveCount    == i);
            REQUIRE(ctx.nKeepaliveSent     == i);
            REQUIRE(ctx.nTimestamp         == nNow);
            REQUIRE(ctx.nLastKeepaliveTime == nNow);
        }
    }

    SECTION("Original context is not modified (immutability check)")
    {
        MiningContext original;
        original.nKeepaliveCount = 10u;
        original.nKeepaliveSent  = 7u;
        original.nTimestamp      = 5000u;

        /* Apply health update to produce a new context */
        MiningContext updated = StatelessMiner::ApplyKeepaliveHealth(original, 9999u);

        /* Original must be unmodified */
        REQUIRE(original.nKeepaliveCount == 10u);
        REQUIRE(original.nKeepaliveSent  == 7u);
        REQUIRE(original.nTimestamp      == 5000u);

        /* Updated must have new values */
        REQUIRE(updated.nKeepaliveCount == 11u);
        REQUIRE(updated.nKeepaliveSent  == 8u);
        REQUIRE(updated.nTimestamp      == 9999u);
    }
}

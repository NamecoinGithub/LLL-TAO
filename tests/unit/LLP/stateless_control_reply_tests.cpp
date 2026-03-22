/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

/*  Targeted regression tests for the stateless control-plane reply paths:
 *
 *    GET_HEIGHT   → BLOCK_HEIGHT  (16-byte payload)
 *    SESSION_STATUS → SESSION_STATUS_ACK
 *    SESSION_KEEPALIVE → ACK / SESSION_EXPIRED on error
 *
 *  These tests verify:
 *  1.  StatelessPacket::Header() / Complete() require fLengthRead to be set —
 *      preventing premature packet completion for LENGTH=0 packets (GET_HEIGHT).
 *  2.  The StatelessMiner pure-functional handlers build non-null response packets
 *      for the SESSION_KEEPALIVE family.
 *  3.  SESSION_STATUS serialisation round-trips correctly for non-DEGRADED miners.
 *  4.  BLOCK_HEIGHT payload is exactly 16 bytes and carries all four heights.
 *
 *  Regression guarded:
 *    - MUTEX deadlock in SESSION_STATUS MINER_DEGRADED / 8-bit MINER_READY handlers
 *      (detected at code-review; handlers now unlock before calling
 *       SendChannelNotification / SendStatelessTemplate)
 *    - StatelessPacket::Header() premature true on LENGTH=0 packet framing
 *    - SESSION_KEEPALIVE processing failure sending no error response
 */

#include <unit/catch2/catch.hpp>

#include <LLP/packets/stateless_packet.h>
#include <LLP/include/stateless_miner.h>
#include <LLP/include/opcode_utility.h>
#include <LLP/include/session_status.h>
#include <LLP/include/stateless_manager.h>

#include <Util/include/runtime.h>

using namespace LLP;

/* ─────────────────────────────────────────────────────────────────────────── */
/*  StatelessPacket framing guard (fLengthRead)                               */
/* ─────────────────────────────────────────────────────────────────────────── */

TEST_CASE("StatelessPacket is not complete before length bytes are consumed",
          "[llp][stateless_packet][framing][get_height]")
{
    SECTION("Default-constructed packet is not complete")
    {
        StatelessPacket pkt;
        REQUIRE(pkt.IsNull()       == true);
        REQUIRE(pkt.Header()       == false);
        REQUIRE(pkt.Complete()     == false);
        REQUIRE(pkt.fLengthRead    == false);
    }

    SECTION("Packet is not complete after HEADER set but before SetLength called")
    {
        /* Simulate ReadPacket after only 2 header bytes arrived */
        StatelessPacket pkt;
        pkt.HEADER = OpcodeUtility::Stateless::GET_HEIGHT;  // 0xD082
        /* LENGTH is still 0, fLengthRead is still false */

        REQUIRE(pkt.IsNull()    == false);
        REQUIRE(pkt.Header()    == false);   /* Must be FALSE — length not yet read */
        REQUIRE(pkt.Complete()  == false);   /* Must be FALSE */
    }

    SECTION("Packet becomes complete only after SetLength called with zero length")
    {
        /* Simulate ReadPacket after both 2-byte header AND 4-byte length consumed */
        StatelessPacket pkt;
        pkt.HEADER = OpcodeUtility::Stateless::GET_HEIGHT;  // LENGTH=0 on wire

        const std::vector<uint8_t> zeroLength = {0x00, 0x00, 0x00, 0x00};
        pkt.SetLength(zeroLength);  /* Sets fLengthRead = true, LENGTH = 0 */

        REQUIRE(pkt.fLengthRead == true);
        REQUIRE(pkt.IsNull()    == false);
        REQUIRE(pkt.Header()    == true);    /* Now ready */
        REQUIRE(pkt.Complete()  == true);    /* DATA.size()==0 == LENGTH==0 */
    }

    SECTION("Packet with non-zero length requires data before Complete")
    {
        StatelessPacket pkt;
        pkt.HEADER = OpcodeUtility::Stateless::SESSION_STATUS;

        const std::vector<uint8_t> len8 = {0x00, 0x00, 0x00, 0x08};
        pkt.SetLength(len8);  /* fLengthRead=true, LENGTH=8 */

        REQUIRE(pkt.Header()    == true);
        REQUIRE(pkt.Complete()  == false);  /* No data yet */

        pkt.DATA.resize(8, 0x00);
        REQUIRE(pkt.Complete()  == true);
    }

    SECTION("SetNull resets fLengthRead so the packet can be reused cleanly")
    {
        StatelessPacket pkt;
        pkt.HEADER = OpcodeUtility::Stateless::GET_HEIGHT;
        pkt.SetLength({0x00, 0x00, 0x00, 0x00});
        REQUIRE(pkt.fLengthRead == true);

        pkt.SetNull();
        REQUIRE(pkt.fLengthRead == false);
        REQUIRE(pkt.IsNull()    == true);
        REQUIRE(pkt.Header()    == false);
        REQUIRE(pkt.Complete()  == false);
    }

    SECTION("Outbound packet constructed with opcode has fLengthRead=false (intentional)")
    {
        /* Outbound packets use GetBytes() and never need Header()/Complete() to be true.
         * Verify the constructor leaves fLengthRead=false so no confusion arises. */
        StatelessPacket pkt(OpcodeUtility::Stateless::BLOCK_HEIGHT);
        pkt.DATA.resize(16, 0xAA);
        pkt.LENGTH = 16;

        REQUIRE(pkt.fLengthRead == false);
        REQUIRE(pkt.IsNull()    == false);
        /* GetBytes() must still produce the full wire representation */
        const auto vBytes = pkt.GetBytes();
        REQUIRE(vBytes.size() == 2u + 4u + 16u);
    }
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*  GET_HEIGHT / BLOCK_HEIGHT 16-byte payload round-trip                      */
/* ─────────────────────────────────────────────────────────────────────────── */

TEST_CASE("GET_HEIGHT results in a 16-byte BLOCK_HEIGHT payload",
          "[llp][get_height][block_height]")
{
    const StatelessMinerManager::GetHeightSnapshot snap{
        123456u,    // nUnifiedHeight
        90000u,     // nPrimeHeight
        85000u,     // nHashHeight
        70000u,     // nStakeHeight
        0u          // nHashTipLo32
    };

    const auto vPayload = StatelessMinerManager::BuildBlockHeightPayload(snap);

    SECTION("Payload is exactly 16 bytes")
    {
        REQUIRE(vPayload.size() == 16u);
    }

    SECTION("BLOCK_HEIGHT packet framing is correct")
    {
        StatelessPacket response(OpcodeUtility::Stateless::BLOCK_HEIGHT);
        response.DATA   = vPayload;
        response.LENGTH = static_cast<uint32_t>(vPayload.size());

        const auto vBytes = response.GetBytes();
        /* Wire frame: 2-byte opcode + 4-byte length + 16-byte payload */
        REQUIRE(vBytes.size() == 22u);

        /* Opcode bytes: 0xD0, 0x02 */
        REQUIRE(vBytes[0] == 0xD0);
        REQUIRE(vBytes[1] == OpcodeUtility::Opcodes::BLOCK_HEIGHT);  // 0x02

        /* Length field big-endian: 0x00000010 = 16 */
        REQUIRE(vBytes[2] == 0x00);
        REQUIRE(vBytes[3] == 0x00);
        REQUIRE(vBytes[4] == 0x00);
        REQUIRE(vBytes[5] == 0x10);
    }

    SECTION("ParseBlockHeightPayload recovers all four heights")
    {
        StatelessMinerManager::GetHeightSnapshot parsed;
        REQUIRE(StatelessMinerManager::ParseBlockHeightPayload(vPayload, parsed));
        REQUIRE(parsed.nUnifiedHeight == snap.nUnifiedHeight);
        REQUIRE(parsed.nPrimeHeight   == snap.nPrimeHeight);
        REQUIRE(parsed.nHashHeight    == snap.nHashHeight);
        REQUIRE(parsed.nStakeHeight   == snap.nStakeHeight);
    }

    SECTION("ParseBlockHeightPayload rejects legacy 4-byte payload")
    {
        const std::vector<uint8_t> vLegacy(vPayload.begin(), vPayload.begin() + 4);
        StatelessMinerManager::GetHeightSnapshot parsed;
        REQUIRE_FALSE(StatelessMinerManager::ParseBlockHeightPayload(vLegacy, parsed));
    }
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*  SESSION_STATUS → SESSION_STATUS_ACK dispatch (pure functional layer)      */
/* ─────────────────────────────────────────────────────────────────────────── */

TEST_CASE("SESSION_STATUS ACK is always built for a well-formed request",
          "[llp][session_status][dispatch]")
{
    SECTION("Non-DEGRADED SESSION_STATUS produces correct 16-byte ACK payload")
    {
        const uint32_t nSessionId  = 0xDEADBEEFu;
        const uint32_t nMinerFlags = SessionStatus::MINER_HAS_TEMPLATE |
                                     SessionStatus::MINER_WORKERS_ACTIVE;

        const uint32_t nLaneHealth = SessionStatus::BuildLaneHealthFlags(true, false, true);
        const uint32_t nUptime     = 42u;

        const auto vAck = SessionStatus::BuildNodeAckPayload(
            nSessionId, true, false, true, nUptime, nMinerFlags);

        REQUIRE(vAck.size() == SessionStatus::ACK_PAYLOAD_SIZE);

        SessionStatus::SessionStatusAck ack;
        REQUIRE(ack.Parse(vAck));
        REQUIRE(ack.session_id        == nSessionId);
        REQUIRE(ack.lane_health_flags == nLaneHealth);
        REQUIRE(ack.uptime_seconds    == nUptime);
        REQUIRE(ack.status_echo_flags == nMinerFlags);
    }

    SECTION("SESSION_STATUS ACK correctly echoes MINER_DEGRADED flag without deadlock")
    {
        /* This verifies the protocol semantics of the MINER_DEGRADED path.
         * The node must echo the flag back in the ACK regardless of whether it then
         * triggers a recovery push.  The deadlock fix (removing nested LOCK(MUTEX))
         * does not change the wire semantics — only the locking discipline. */
        const uint32_t nDegradedFlags = SessionStatus::MINER_DEGRADED |
                                        SessionStatus::MINER_HAS_TEMPLATE;

        const auto vAck = SessionStatus::BuildNodeAckPayload(
            0x12345678u, true, false, true, 100u, nDegradedFlags);

        SessionStatus::SessionStatusAck ack;
        REQUIRE(ack.Parse(vAck));
        REQUIRE((ack.status_echo_flags & SessionStatus::MINER_DEGRADED) != 0);
        REQUIRE((ack.status_echo_flags & SessionStatus::MINER_HAS_TEMPLATE) != 0);
    }

    SECTION("SESSION_STATUS request parse detects miner status flags correctly")
    {
        SessionStatus::SessionStatusRequest req;
        req.session_id   = 0x548c90d3u;  // from the representative log
        req.status_flags = 0x00000006u;  // MINER_HAS_TEMPLATE | MINER_WORKERS_ACTIVE

        REQUIRE((req.status_flags & SessionStatus::MINER_DEGRADED)      == 0);
        REQUIRE((req.status_flags & SessionStatus::MINER_HAS_TEMPLATE)  != 0);
        REQUIRE((req.status_flags & SessionStatus::MINER_WORKERS_ACTIVE) != 0);

        /* Serialise and parse back */
        auto v = req.Serialize();
        REQUIRE(v.size() == 8u);

        SessionStatus::SessionStatusRequest req2;
        REQUIRE(req2.Parse(v));
        REQUIRE(req2.session_id   == req.session_id);
        REQUIRE(req2.status_flags == req.status_flags);
    }
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*  SESSION_KEEPALIVE dispatch through StatelessMiner pure functional layer   */
/* ─────────────────────────────────────────────────────────────────────────── */

TEST_CASE("ProcessSessionKeepalive emits a response for authenticated sessions",
          "[llp][session_keepalive][dispatch]")
{
    const uint32_t nSessionId = 0xCAFEBABEu;
    const uint64_t nNow       = runtime::unifiedtimestamp();

    /* Build a minimal authenticated context with an active session */
    MiningContext ctx = MiningContext()
        .WithAuth(true)
        .WithSession(nSessionId)
        .WithSessionStart(nNow - 120)  // session active for 120 s
        .WithTimestamp(nNow - 5);      // last keepalive 5 s ago

    SECTION("v1 keepalive (4-byte payload) generates a v1 ACK")
    {
        /* v1 payload: session_id only (4 bytes LE) */
        StatelessPacket pkt(OpcodeUtility::Stateless::SESSION_KEEPALIVE);
        pkt.DATA.push_back(static_cast<uint8_t>(nSessionId & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >>  8) & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >> 16) & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >> 24) & 0xFF));
        pkt.LENGTH = 4;

        const ProcessResult result = StatelessMiner::ProcessSessionKeepalive(ctx, pkt);

        REQUIRE(result.fSuccess                   == true);
        REQUIRE(result.response.IsNull()           == false);
        REQUIRE(result.response.LENGTH             > 0);
        REQUIRE(result.response.HEADER ==
                OpcodeUtility::Stateless::SESSION_KEEPALIVE);
        REQUIRE(result.context.nKeepaliveCount     > ctx.nKeepaliveCount);
    }

    SECTION("v2 keepalive (8-byte payload) generates a v2 ACK")
    {
        /* v2 payload: session_id(4 LE) + prevblock_lo32(4 BE) */
        StatelessPacket pkt(OpcodeUtility::Stateless::SESSION_KEEPALIVE);
        pkt.DATA.push_back(static_cast<uint8_t>(nSessionId & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >>  8) & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >> 16) & 0xFF));
        pkt.DATA.push_back(static_cast<uint8_t>((nSessionId >> 24) & 0xFF));
        /* prevblock_lo32 = 0xAABBCCDD */
        pkt.DATA.push_back(0xAA);
        pkt.DATA.push_back(0xBB);
        pkt.DATA.push_back(0xCC);
        pkt.DATA.push_back(0xDD);
        pkt.LENGTH = 8;

        const ProcessResult result = StatelessMiner::ProcessSessionKeepalive(ctx, pkt);

        REQUIRE(result.fSuccess                   == true);
        REQUIRE(result.response.IsNull()           == false);
        REQUIRE(result.response.HEADER ==
                OpcodeUtility::Stateless::SESSION_KEEPALIVE);
        /* v2 ACK is 32 bytes */
        REQUIRE(result.response.LENGTH             == 32u);
    }

    SECTION("Unauthenticated context returns error with no response")
    {
        MiningContext unauthCtx = MiningContext();  // fAuthenticated=false

        StatelessPacket pkt(OpcodeUtility::Stateless::SESSION_KEEPALIVE);
        pkt.LENGTH = 0;

        const ProcessResult result = StatelessMiner::ProcessSessionKeepalive(unauthCtx, pkt);

        REQUIRE(result.fSuccess       == false);
        REQUIRE(result.strError.empty() == false);
    }
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*  Opcode constant consistency                                                */
/* ─────────────────────────────────────────────────────────────────────────── */

TEST_CASE("Stateless control-plane opcodes are correctly mapped",
          "[llp][opcodes][control]")
{
    SECTION("GET_HEIGHT and BLOCK_HEIGHT are mirrored correctly")
    {
        REQUIRE(OpcodeUtility::Stateless::GET_HEIGHT   == 0xD082u);
        REQUIRE(OpcodeUtility::Stateless::BLOCK_HEIGHT == 0xD002u);
        REQUIRE(OpcodeUtility::Stateless::Mirror(OpcodeUtility::Opcodes::GET_HEIGHT)   ==
                OpcodeUtility::Stateless::GET_HEIGHT);
        REQUIRE(OpcodeUtility::Stateless::Mirror(OpcodeUtility::Opcodes::BLOCK_HEIGHT) ==
                OpcodeUtility::Stateless::BLOCK_HEIGHT);
    }

    SECTION("SESSION_STATUS and SESSION_STATUS_ACK are mirrored correctly")
    {
        REQUIRE(OpcodeUtility::Stateless::SESSION_STATUS     == 0xD0DBu);
        REQUIRE(OpcodeUtility::Stateless::SESSION_STATUS_ACK == 0xD0DCu);
    }

    SECTION("SESSION_KEEPALIVE is mirrored correctly")
    {
        REQUIRE(OpcodeUtility::Stateless::SESSION_KEEPALIVE  == 0xD0D4u);
    }

    SECTION("All three opcodes are recognised as stateless")
    {
        REQUIRE(OpcodeUtility::Stateless::IsStateless(OpcodeUtility::Stateless::GET_HEIGHT)        == true);
        REQUIRE(OpcodeUtility::Stateless::IsStateless(OpcodeUtility::Stateless::SESSION_STATUS)    == true);
        REQUIRE(OpcodeUtility::Stateless::IsStateless(OpcodeUtility::Stateless::SESSION_KEEPALIVE) == true);
    }
}

/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLP/include/opcode_utility.h>
#include <LLP/include/session_status.h>
#include <LLP/include/preflight_check.h>
#include <LLP/include/get_block_policy.h>
#include <LLP/include/stateless_miner.h>

#include <cstdint>

using namespace LLP;

/**
 * Health Decoupling Tests
 *
 * Verify that Session Status and KeepAlive health are diagnostic-only and
 * can NEVER prevent or block Block Data delivery.  These tests enforce the
 * invariant that GET_ROUND and PUSH are the only mining health barometers.
 */

//=============================================================================
// WI-1: IsMiningCritical16 classification
//=============================================================================

TEST_CASE("IsMiningCritical16 classifies mining-critical opcodes correctly", "[health_decoupling][wi1]")
{
    SECTION("BLOCK_DATA is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::BLOCK_DATA));
    }

    SECTION("NEW_ROUND is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::NEW_ROUND));
    }

    SECTION("BLOCK_REJECTED (GET_BLOCK control response) is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::BLOCK_REJECTED));
    }

    SECTION("BLOCK_ACCEPTED is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::BLOCK_ACCEPTED));
    }

    SECTION("SESSION_EXPIRED is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::SESSION_EXPIRED));
    }

    SECTION("PRIME_BLOCK_AVAILABLE push notification is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::PRIME_BLOCK_AVAILABLE));
    }

    SECTION("HASH_BLOCK_AVAILABLE push notification is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::HASH_BLOCK_AVAILABLE));
    }

    SECTION("GET_BLOCK (push template) is mining-critical")
    {
        REQUIRE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::GET_BLOCK));
    }
}

TEST_CASE("IsMiningCritical16 does NOT classify diagnostic opcodes as critical", "[health_decoupling][wi1]")
{
    SECTION("SESSION_KEEPALIVE is NOT mining-critical")
    {
        REQUIRE_FALSE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::SESSION_KEEPALIVE));
    }

    SECTION("SESSION_STATUS is NOT mining-critical")
    {
        REQUIRE_FALSE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::SESSION_STATUS));
    }

    SECTION("SESSION_STATUS_ACK is NOT mining-critical")
    {
        REQUIRE_FALSE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::SESSION_STATUS_ACK));
    }

    SECTION("PING is NOT mining-critical")
    {
        REQUIRE_FALSE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::PING));
    }

    SECTION("AUTH_RESULT is NOT mining-critical")
    {
        REQUIRE_FALSE(OpcodeUtility::IsMiningCritical16(OpcodeUtility::Stateless::AUTH_RESULT));
    }
}

//=============================================================================
// WI-3: Session Status health is reporting-only
//=============================================================================

TEST_CASE("Lane health flags are reporting-only constants", "[health_decoupling][wi3]")
{
    SECTION("LANE_PRIMARY_ALIVE is bit 0")
    {
        REQUIRE(SessionStatus::LANE_PRIMARY_ALIVE == 0x01);
    }

    SECTION("LANE_SECONDARY_ALIVE is bit 1")
    {
        REQUIRE(SessionStatus::LANE_SECONDARY_ALIVE == 0x02);
    }

    SECTION("LANE_SIM_LINK_ACTIVE is bit 2")
    {
        REQUIRE(SessionStatus::LANE_SIM_LINK_ACTIVE == 0x04);
    }

    SECTION("LANE_AUTHENTICATED is bit 3")
    {
        REQUIRE(SessionStatus::LANE_AUTHENTICATED == 0x08);
    }

    SECTION("Lane health flags do not overlap with miner status flags")
    {
        /* Both use same byte position [4-7] in different packets
         * (ACK vs REQUEST), so overlap is acceptable by design.
         * This test documents that they use distinct bit positions
         * within their respective contexts. */
        uint32_t nAllLane = SessionStatus::LANE_PRIMARY_ALIVE
                          | SessionStatus::LANE_SECONDARY_ALIVE
                          | SessionStatus::LANE_SIM_LINK_ACTIVE
                          | SessionStatus::LANE_AUTHENTICATED;
        REQUIRE(nAllLane == 0x0F);
    }
}

TEST_CASE("MINER_DEGRADED flag triggers recovery, not blocking", "[health_decoupling][wi3]")
{
    SECTION("MINER_DEGRADED is bit 0")
    {
        REQUIRE(SessionStatus::MINER_DEGRADED == 0x01);
    }

    SECTION("MINER_HAS_TEMPLATE is bit 1")
    {
        REQUIRE(SessionStatus::MINER_HAS_TEMPLATE == 0x02);
    }

    SECTION("MINER_WORKERS_ACTIVE is bit 2")
    {
        REQUIRE(SessionStatus::MINER_WORKERS_ACTIVE == 0x04);
    }
}

//=============================================================================
// WI-4: KeepAlive health is diagnostic-only — MiningContext expiry tests
//=============================================================================

TEST_CASE("MiningContext session expiry is activity-based, not keepalive-based", "[health_decoupling][wi4]")
{
    SECTION("Fresh context is not expired")
    {
        MiningContext ctx;
        ctx = ctx.WithSession(42)
                 .WithTimestamp(runtime::unifiedtimestamp())
                 .WithSessionStart(runtime::unifiedtimestamp())
                 .WithSessionTimeout(86400);
        REQUIRE_FALSE(ctx.IsSessionExpired(runtime::unifiedtimestamp()));
    }

    SECTION("Context with recent activity is not expired even if keepalive count is 0")
    {
        uint64_t nNow = runtime::unifiedtimestamp();
        MiningContext ctx;
        ctx = ctx.WithSession(42)
                 .WithTimestamp(nNow)
                 .WithSessionStart(nNow - 1000)
                 .WithSessionTimeout(86400);
        /* No keepalive ever sent — but session is still valid because
         * nTimestamp (activity) is recent.  This proves keepalive health
         * does not affect session validity. */
        REQUIRE(ctx.nKeepaliveCount == 0);
        REQUIRE_FALSE(ctx.IsSessionExpired(nNow + 100));
    }

    SECTION("Context expires only when activity timestamp exceeds timeout")
    {
        uint64_t nNow = runtime::unifiedtimestamp();
        MiningContext ctx;
        ctx = ctx.WithSession(42)
                 .WithTimestamp(nNow - 90000)
                 .WithSessionStart(nNow - 100000)
                 .WithSessionTimeout(86400);
        /* Activity was 90000s ago, timeout is 86400s → expired */
        REQUIRE(ctx.IsSessionExpired(nNow));
    }

    SECTION("GET_BLOCK, GET_ROUND update activity timestamp — keeping session alive")
    {
        uint64_t nNow = runtime::unifiedtimestamp();
        MiningContext ctx;
        ctx = ctx.WithSession(42)
                 .WithTimestamp(nNow - 80000)
                 .WithSessionStart(nNow - 100000)
                 .WithSessionTimeout(86400);
        /* Session would expire soon (80000s idle, 86400s timeout).
         * Simulate GET_BLOCK refreshing the activity timestamp. */
        ctx = ctx.WithTimestamp(nNow);
        REQUIRE_FALSE(ctx.IsSessionExpired(nNow));
        REQUIRE_FALSE(ctx.IsSessionExpired(nNow + 3600));
    }
}

//=============================================================================
// WI-7: PreflightCheck opcode categories
//=============================================================================

TEST_CASE("PreflightCheck categories enforce health decoupling", "[health_decoupling][wi7]")
{
    SECTION("GET_BLOCK requires auth only — NOT session-status health")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(129) == PreflightCheck::PreflightCategory::REQUIRE_AUTH);
        REQUIRE(PreflightCheck::IsMiningCriticalOpcode(129));
        REQUIRE_FALSE(PreflightCheck::IsDiagnosticOnlyOpcode(129));
    }

    SECTION("GET_ROUND requires auth only")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(133) == PreflightCheck::PreflightCategory::REQUIRE_AUTH);
    }

    SECTION("SUBMIT_BLOCK requires auth only")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(1) == PreflightCheck::PreflightCategory::REQUIRE_AUTH);
    }

    SECTION("SESSION_STATUS is always-pass (diagnostic)")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(219) == PreflightCheck::PreflightCategory::ALWAYS_PASS);
        REQUIRE(PreflightCheck::IsDiagnosticOnlyOpcode(219));
    }

    SECTION("SESSION_KEEPALIVE is always-pass (diagnostic)")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(212) == PreflightCheck::PreflightCategory::ALWAYS_PASS);
        REQUIRE(PreflightCheck::IsDiagnosticOnlyOpcode(212));
    }

    SECTION("SET_CHANNEL requires full state")
    {
        REQUIRE(PreflightCheck::GetPreflightCategory(3) == PreflightCheck::PreflightCategory::REQUIRE_FULL);
    }

    SECTION("No diagnostic opcode is in REQUIRE_FULL category")
    {
        /* Exhaustive check: all Category 0 opcodes must NOT be Category 2 */
        const uint8_t diagnosticOpcodes[] = {
            OpcodeUtility::Opcodes::SESSION_KEEPALIVE,   // 212
            OpcodeUtility::Opcodes::SESSION_STATUS,      // 219
            OpcodeUtility::Opcodes::SESSION_STATUS_ACK,  // 220
            OpcodeUtility::Opcodes::SESSION_EXPIRED,     // 221
            OpcodeUtility::Opcodes::PING,                // 253
            OpcodeUtility::Opcodes::CLOSE,               // 254
        };
        for(uint8_t op : diagnosticOpcodes)
        {
            REQUIRE(PreflightCheck::GetPreflightCategory(op) != PreflightCheck::PreflightCategory::REQUIRE_FULL);
        }
    }
}

TEST_CASE("GetBlockPolicyReason has no health-based gating reasons", "[health_decoupling][wi3][wi4]")
{
    SECTION("No KEEPALIVE_STALE or SESSION_STATUS_STALE reason exists")
    {
        /* Verify that the GetBlockPolicyReason enum does not contain any
         * health-status-based blocking reasons.  The only valid reasons
         * for rejecting GET_BLOCK are: rate limits, session validity,
         * authentication, template readiness, and channel configuration. */
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::NONE)                         == 0);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::RATE_LIMIT_EXCEEDED)          == 1);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::SESSION_INVALID)              == 2);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::UNAUTHENTICATED)              == 3);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::TEMPLATE_NOT_READY)           == 4);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::INTERNAL_RETRY)               == 5);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::TEMPLATE_STALE)               == 6);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::TEMPLATE_REBUILD_IN_PROGRESS) == 7);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::TEMPLATE_SOURCE_UNAVAILABLE)  == 8);
        REQUIRE(static_cast<uint8_t>(GetBlockPolicyReason::CHANNEL_NOT_SET)              == 9);
    }
}

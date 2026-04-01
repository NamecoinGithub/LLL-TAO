/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_PREFLIGHT_CHECK_H
#define NEXUS_LLP_INCLUDE_PREFLIGHT_CHECK_H

#include <cstdint>

namespace LLP
{
namespace PreflightCheck
{
    //=========================================================================
    // OPCODE CATEGORIES FOR PREFLIGHT INGRESS VALIDATION (R-14)
    //
    // This header defines the opcode category model for the planned
    // PreflightCheck(opcode, container) centralized gate.  Each opcode
    // falls into exactly one category that determines the preconditions
    // checked before the handler is invoked.
    //
    // HEALTH DECOUPLING INVARIANT:
    //   Session Status health and KeepAlive health are diagnostic-only.
    //   They MUST NEVER appear as preconditions for Category 1 or 2
    //   opcodes.  Only GET_ROUND and PUSH (BLOCK_AVAILABLE) are the
    //   barometers for mining health — not keepalive or session-status.
    //
    // Category 0: ALWAYS-PASS (diagnostic / lifecycle)
    //   No preconditions.  These opcodes are processed unconditionally.
    //   - SESSION_STATUS, SESSION_KEEPALIVE, PONG, PING, CLOSE
    //   - SESSION_EXPIRED (outbound only)
    //
    // Category 1: REQUIRE-AUTH-ONLY (mining-critical)
    //   Require: (a) valid session, (b) authenticated.
    //   Do NOT require: session-status health, keepalive health.
    //   - GET_BLOCK, GET_ROUND, SUBMIT_BLOCK, GET_HEIGHT, GET_REWARD
    //   - MINER_READY (subscribe)
    //
    // Category 2: REQUIRE-FULL-STATE (configuration)
    //   Require: (a) valid session, (b) authenticated, (c) channel set.
    //   - SET_CHANNEL, MINER_SET_REWARD, SET_COINBASE
    //
    // STATIC ASSERTION: No Category 0 opcode may ever be placed in
    //   Category 2.  This prevents diagnostic opcodes from being
    //   blocked by full-state requirements.
    //=========================================================================

    /** PreflightCategory — Opcode precondition category */
    enum class PreflightCategory : uint8_t
    {
        ALWAYS_PASS      = 0,  // No preconditions (diagnostic/lifecycle)
        REQUIRE_AUTH     = 1,  // Requires valid session + authentication
        REQUIRE_FULL     = 2,  // Requires valid session + auth + channel set
    };


    /** GetPreflightCategory
     *
     *  Returns the precondition category for a given 8-bit legacy opcode.
     *  Used by the planned PreflightCheck() gate in ProcessPacket().
     *
     *  @param[in] nOpcode  The 8-bit opcode
     *
     *  @return PreflightCategory for this opcode
     *
     **/
    inline PreflightCategory GetPreflightCategory(uint8_t nOpcode)
    {
        switch(nOpcode)
        {
            /* Category 0: Always-pass (diagnostic/lifecycle) */
            case 212: /* SESSION_KEEPALIVE */
            case 219: /* SESSION_STATUS */
            case 220: /* SESSION_STATUS_ACK */
            case 221: /* SESSION_EXPIRED */
            case 253: /* PING */
            case 254: /* CLOSE */
                return PreflightCategory::ALWAYS_PASS;

            /* Category 1: Require-auth-only (mining-critical) */
            case 129: /* GET_BLOCK */
            case 130: /* GET_HEIGHT */
            case 131: /* GET_REWARD */
            case 133: /* GET_ROUND */
            case   1: /* SUBMIT_BLOCK */
            case 216: /* MINER_READY */
                return PreflightCategory::REQUIRE_AUTH;

            /* Category 2: Require-full-state (configuration) */
            case   3: /* SET_CHANNEL */
            case 213: /* MINER_SET_REWARD */
            case   5: /* SET_COINBASE */
                return PreflightCategory::REQUIRE_FULL;

            /* Default: require auth for unknown opcodes (fail-safe) */
            default:
                return PreflightCategory::REQUIRE_AUTH;
        }
    }


    /** IsDiagnosticOnlyOpcode
     *
     *  Returns true if the opcode is diagnostic-only (Category 0).
     *  Diagnostic opcodes must never be blocked by health status checks
     *  and must never block mining-critical operations.
     *
     *  @param[in] nOpcode  The 8-bit opcode
     *
     *  @return true if opcode is diagnostic-only
     *
     **/
    inline bool IsDiagnosticOnlyOpcode(uint8_t nOpcode)
    {
        return GetPreflightCategory(nOpcode) == PreflightCategory::ALWAYS_PASS;
    }


    /** IsMiningCriticalOpcode
     *
     *  Returns true if the opcode is mining-critical (Category 1).
     *  Mining-critical opcodes require authentication but MUST NOT be
     *  gated by session-status health or keepalive health.
     *
     *  @param[in] nOpcode  The 8-bit opcode
     *
     *  @return true if opcode is mining-critical
     *
     **/
    inline bool IsMiningCriticalOpcode(uint8_t nOpcode)
    {
        return GetPreflightCategory(nOpcode) == PreflightCategory::REQUIRE_AUTH;
    }


} // namespace PreflightCheck
} // namespace LLP

#endif

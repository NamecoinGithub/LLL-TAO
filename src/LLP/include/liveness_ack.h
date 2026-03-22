/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_LIVENESS_ACK_H
#define NEXUS_LLP_INCLUDE_LIVENESS_ACK_H

#include <cstdint>
#include <vector>

/** LLP::LivenessAck
 *
 *  Centralized helpers for session-liveness ACK/keepalive packet families.
 *
 *  This header intentionally avoids TAO::Ledger and MiningContext includes so
 *  it can be used as a lightweight, test-friendly utility layer.  All helpers
 *  that require live chain state or MiningContext are declared in
 *  StatelessMiner (stateless_miner.h / stateless_miner.cpp).
 *
 *  Packet families covered:
 *    SESSION_KEEPALIVE     — legacy (0xD4) and stateless mirror (0xD0D4)
 *    KEEPALIVE_V2          — stateless-only (0xD100)
 *    KEEPALIVE_V2_ACK      — stateless-only node response (0xD101)
 *    SESSION_STATUS        — miner health query (0xDB / 0xD0DB)
 *    SESSION_STATUS_ACK    — node lane-health response (0xDC / 0xD0DC)
 *    SESSION_EXPIRED       — session termination signal (0xDD / 0xD0DD)
 *
 **/
namespace LLP
{
namespace LivenessAck
{

    //=========================================================================
    // AckPacketFamily — canonical classification of liveness/ACK opcodes
    //=========================================================================

    /** AckPacketFamily
     *
     *  Enum identifying which logical ACK/liveness family a raw opcode belongs
     *  to.  Used to drive unified logging and routing decisions without
     *  scattering opcode comparisons across the codebase.
     *
     **/
    enum class AckPacketFamily : uint8_t
    {
        SESSION_KEEPALIVE,     ///< SESSION_KEEPALIVE on either legacy or stateless lane
        KEEPALIVE_V2,          ///< KEEPALIVE_V2 request (stateless-only)
        KEEPALIVE_V2_ACK,      ///< KEEPALIVE_V2_ACK response (stateless-only)
        SESSION_STATUS,        ///< SESSION_STATUS health query (either lane)
        SESSION_STATUS_ACK,    ///< SESSION_STATUS_ACK response (either lane)
        SESSION_EXPIRED,       ///< SESSION_EXPIRED notification (either lane)
        UNKNOWN                ///< Not a liveness/ACK packet
    };


    //=========================================================================
    // Opcode constants (duplicated here to avoid pulling in opcode_utility.h)
    //=========================================================================
    namespace Opcodes
    {
        /* Legacy 8-bit opcodes */
        static constexpr uint16_t SESSION_KEEPALIVE_LEGACY  = 212u;   // 0xD4
        static constexpr uint16_t SESSION_STATUS_LEGACY     = 219u;   // 0xDB
        static constexpr uint16_t SESSION_STATUS_ACK_LEGACY = 220u;   // 0xDC
        static constexpr uint16_t SESSION_EXPIRED_LEGACY    = 221u;   // 0xDD

        /* Stateless 16-bit mirrored opcodes */
        static constexpr uint16_t SESSION_KEEPALIVE_STATELESS  = 0xD0D4u;
        static constexpr uint16_t SESSION_STATUS_STATELESS      = 0xD0DBu;
        static constexpr uint16_t SESSION_STATUS_ACK_STATELESS  = 0xD0DCu;
        static constexpr uint16_t SESSION_EXPIRED_STATELESS     = 0xD0DDu;

        /* Stateless-only opcodes */
        static constexpr uint16_t KEEPALIVE_V2     = 0xD100u;
        static constexpr uint16_t KEEPALIVE_V2_ACK = 0xD101u;
    }


    //=========================================================================
    // ClassifyPacket — opcode → AckPacketFamily
    //=========================================================================

    /** ClassifyPacket
     *
     *  Map a raw opcode (8-bit or 16-bit) to the logical ACK/liveness family.
     *  Returns AckPacketFamily::UNKNOWN for opcodes outside the liveness group.
     *
     *  @param[in] opcode  Raw packet opcode (both 8-bit and stateless 16-bit forms accepted)
     *
     *  @return AckPacketFamily enum value
     *
     **/
    inline AckPacketFamily ClassifyPacket(uint16_t opcode) noexcept
    {
        switch(opcode)
        {
            case Opcodes::SESSION_KEEPALIVE_LEGACY:
            case Opcodes::SESSION_KEEPALIVE_STATELESS:
                return AckPacketFamily::SESSION_KEEPALIVE;

            case Opcodes::KEEPALIVE_V2:
                return AckPacketFamily::KEEPALIVE_V2;

            case Opcodes::KEEPALIVE_V2_ACK:
                return AckPacketFamily::KEEPALIVE_V2_ACK;

            case Opcodes::SESSION_STATUS_LEGACY:
            case Opcodes::SESSION_STATUS_STATELESS:
                return AckPacketFamily::SESSION_STATUS;

            case Opcodes::SESSION_STATUS_ACK_LEGACY:
            case Opcodes::SESSION_STATUS_ACK_STATELESS:
                return AckPacketFamily::SESSION_STATUS_ACK;

            case Opcodes::SESSION_EXPIRED_LEGACY:
            case Opcodes::SESSION_EXPIRED_STATELESS:
                return AckPacketFamily::SESSION_EXPIRED;

            default:
                return AckPacketFamily::UNKNOWN;
        }
    }


    /** AckFamilyName
     *
     *  Human-readable name for an AckPacketFamily value (for logging).
     *
     *  @param[in] family  ACK packet family
     *
     *  @return C-string name, never nullptr
     *
     **/
    inline const char* AckFamilyName(AckPacketFamily family) noexcept
    {
        switch(family)
        {
            case AckPacketFamily::SESSION_KEEPALIVE:  return "SESSION_KEEPALIVE";
            case AckPacketFamily::KEEPALIVE_V2:       return "KEEPALIVE_V2";
            case AckPacketFamily::KEEPALIVE_V2_ACK:   return "KEEPALIVE_V2_ACK";
            case AckPacketFamily::SESSION_STATUS:     return "SESSION_STATUS";
            case AckPacketFamily::SESSION_STATUS_ACK: return "SESSION_STATUS_ACK";
            case AckPacketFamily::SESSION_EXPIRED:    return "SESSION_EXPIRED";
            default:                                  return "UNKNOWN";
        }
    }


    //=========================================================================
    // AckLogDirection / AckLogTag — canonical liveness logging tags
    //=========================================================================

    enum class AckLogDirection : uint8_t
    {
        REQUEST,   ///< inbound miner -> node liveness/control packet
        ACK,       ///< outbound node -> miner response
        NOTIFY     ///< outbound node -> miner notification
    };


    inline const char* AckLogTag(AckPacketFamily family, AckLogDirection direction) noexcept
    {
        switch(family)
        {
            case AckPacketFamily::SESSION_KEEPALIVE:
                switch(direction)
                {
                    case AckLogDirection::REQUEST: return "[liveness:SESSION_KEEPALIVE/req]";
                    case AckLogDirection::ACK:     return "[liveness:SESSION_KEEPALIVE/ack]";
                    case AckLogDirection::NOTIFY:  return "[liveness:SESSION_KEEPALIVE/notify]";
                }
                break;

            case AckPacketFamily::KEEPALIVE_V2:
            case AckPacketFamily::KEEPALIVE_V2_ACK:
                switch(direction)
                {
                    case AckLogDirection::REQUEST: return "[liveness:KEEPALIVE_V2/req]";
                    case AckLogDirection::ACK:     return "[liveness:KEEPALIVE_V2/ack]";
                    case AckLogDirection::NOTIFY:  return "[liveness:KEEPALIVE_V2/notify]";
                }
                break;

            case AckPacketFamily::SESSION_STATUS:
            case AckPacketFamily::SESSION_STATUS_ACK:
                switch(direction)
                {
                    case AckLogDirection::REQUEST: return "[liveness:SESSION_STATUS/req]";
                    case AckLogDirection::ACK:     return "[liveness:SESSION_STATUS/ack]";
                    case AckLogDirection::NOTIFY:  return "[liveness:SESSION_STATUS/notify]";
                }
                break;

            case AckPacketFamily::SESSION_EXPIRED:
                switch(direction)
                {
                    case AckLogDirection::REQUEST: return "[liveness:SESSION_EXPIRED/req]";
                    case AckLogDirection::ACK:     return "[liveness:SESSION_EXPIRED/ack]";
                    case AckLogDirection::NOTIFY:  return "[liveness:SESSION_EXPIRED/notify]";
                }
                break;

            default:
                switch(direction)
                {
                    case AckLogDirection::REQUEST: return "[liveness:UNKNOWN/req]";
                    case AckLogDirection::ACK:     return "[liveness:UNKNOWN/ack]";
                    case AckLogDirection::NOTIFY:  return "[liveness:UNKNOWN/notify]";
                }
                break;
        }

        return "[liveness:UNKNOWN/ack]";
    }


    //=========================================================================
    // ChainHeightSnapshot — atomic chain state for keepalive ACK payloads
    //=========================================================================

    /** ChainHeightSnapshot
     *
     *  Consistent snapshot of all channel heights and the best-chain hash lo32,
     *  gathered atomically from a single tStateBest load to prevent the race
     *  where heights and hash could come from different blocks.
     *
     *  Populated by StatelessMiner::GatherCurrentChainHeights().
     *
     **/
    struct ChainHeightSnapshot
    {
        uint32_t nUnifiedHeight{0};  ///< Best-chain unified height (block.nHeight)
        uint32_t nPrimeHeight{0};    ///< Prime channel height (channel 1)
        uint32_t nHashHeight{0};     ///< Hash channel height (channel 2)
        uint32_t nStakeHeight{0};    ///< Stake channel height (channel 0)
        uint32_t nHashTipLo32{0};    ///< Lo32 of hashBestChain (fork canary cross-check)
    };


    //=========================================================================
    // SESSION_EXPIRED payload builder — canonical 5-byte format
    //=========================================================================

    /** SESSION_EXPIRED reason codes */
    static constexpr uint8_t EXPIRED_REASON_INACTIVITY = 0x01; ///< No keepalive within timeout
    static constexpr uint8_t EXPIRED_REASON_AUTH_LOST  = 0x02; ///< Authentication state lost

    /** BuildSessionExpiredPayload
     *
     *  Build the canonical 5-byte SESSION_EXPIRED wire payload.
     *
     *  Wire format:
     *    [0..3] session_id  — little-endian uint32
     *    [4]    reason      — 0x01=INACTIVITY, 0x02=AUTH_LOST
     *
     *  This is the single authoritative constructor for SESSION_EXPIRED
     *  payloads; callers in ProcessSessionKeepalive and ProcessKeepaliveV2
     *  must use this helper to avoid drift.
     *
     *  @param[in] session_id  Session identifier
     *  @param[in] reason      Reason code (default: EXPIRED_REASON_INACTIVITY)
     *
     *  @return 5-byte serialized payload
     *
     **/
    inline std::vector<uint8_t> BuildSessionExpiredPayload(
        uint32_t session_id,
        uint8_t  reason = EXPIRED_REASON_INACTIVITY)
    {
        std::vector<uint8_t> payload;
        payload.reserve(5);
        payload.push_back(static_cast<uint8_t>( session_id        & 0xFF));
        payload.push_back(static_cast<uint8_t>((session_id >>  8) & 0xFF));
        payload.push_back(static_cast<uint8_t>((session_id >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((session_id >> 24) & 0xFF));
        payload.push_back(reason);
        return payload;
    }

} /* namespace LivenessAck */
} /* namespace LLP */

#endif /* NEXUS_LLP_INCLUDE_LIVENESS_ACK_H */

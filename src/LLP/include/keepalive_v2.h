/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People
____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_KEEPALIVE_V2_H
#define NEXUS_LLP_INCLUDE_KEEPALIVE_V2_H

#include <LLC/types/uint1024.h>

#include <array>
#include <cstdint>
#include <vector>

namespace LLP
{

    /** KeepaliveV2
     *
     *  Common utility for KEEPALIVE v2 protocol handling.
     *  Used by both the Legacy (Miner.cpp) and Stateless (stateless_miner.cpp) lanes.
     *
     *  Protocol versions (distinguished by payload length):
     *
     *  v1 Client → Node (len == 4):
     *    [0..3] session_id (little-endian uint32)
     *
     *  v2 Client → Node (len == 8):
     *    [0..3] session_id            (little-endian uint32)
     *    [4..7] miner_prevblock_suffix (little-endian uint32; last 4 bytes of template
     *                                   hashPrevBlock, or 0 if no template)
     *
     *  v1 Node → Client:
     *    Legacy lane   (len == 4): [0..3] session expiry seconds (little-endian uint32)
     *    Stateless lane (len == 5): [0] status byte (0x01 = active) +
     *                               [1..4] remaining_timeout (little-endian uint32)
     *
     *  v2 Node → Client BESTCURRENT (len == 28):
     *    [0..3]   session_id          (little-endian uint32, same as v1)
     *    [4..7]   unified_height      (big-endian uint32)
     *    [8..11]  prime_height        (big-endian uint32)
     *    [12..15] hash_height         (big-endian uint32)
     *    [16..19] stake_height        (big-endian uint32)
     *    [20..23] nBits               (big-endian uint32; channel-appropriate difficulty)
     *    [24..27] hashBestChain_prefix (first 4 bytes of node hashBestChain via GetBytes())
     *
     **/
    namespace KeepaliveV2
    {

        /** ParsePayload
         *
         *  Parse a keepalive payload to extract session_id and (for v2) miner_prevblock_suffix.
         *
         *  @param[in]  data                   Raw payload bytes
         *  @param[out] nSessionId             Session ID (bytes [0..3], little-endian)
         *  @param[out] prevblockSuffixBytes   Raw bytes [4..7] as-sent (zeroed if payload is v1)
         *
         *  @return true if the payload is v2 (len >= 8), false if v1 (len >= 4 but < 8)
         *          Callers should reject if data.size() < 4.
         *
         **/
        inline bool ParsePayload(
            const std::vector<uint8_t>& data,
            uint32_t& nSessionId,
            std::array<uint8_t, 4>& prevblockSuffixBytes)
        {
            prevblockSuffixBytes = {};

            if(data.size() < 4)
                return false;

            /* Parse session_id (little-endian) */
            nSessionId =
                static_cast<uint32_t>(data[0])        |
                (static_cast<uint32_t>(data[1]) << 8)  |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24);

            if(data.size() >= 8)
            {
                /* v2: copy miner_prevblock_suffix as raw bytes (no endian conversion) */
                prevblockSuffixBytes[0] = data[4];
                prevblockSuffixBytes[1] = data[5];
                prevblockSuffixBytes[2] = data[6];
                prevblockSuffixBytes[3] = data[7];
                return true;
            }

            return false; /* v1 */
        }


        /** BuildBestCurrentResponse
         *
         *  Build a 28-byte BESTCURRENT keepalive v2 response payload.
         *
         *  Layout:
         *    [0..3]   session_id          (little-endian uint32)
         *    [4..7]   unified_height      (big-endian uint32)
         *    [8..11]  prime_height        (big-endian uint32)
         *    [12..15] hash_height         (big-endian uint32)
         *    [16..19] stake_height        (big-endian uint32)
         *    [20..23] nBits               (big-endian uint32)
         *    [24..27] hashBestChain_prefix (first 4 bytes of hashBestChain.GetBytes())
         *
         *  @param[in] nSessionId      Session identifier
         *  @param[in] nUnifiedHeight  Current unified best-chain height
         *  @param[in] nPrimeHeight    Current Prime channel height
         *  @param[in] nHashHeight     Current Hash channel height
         *  @param[in] nStakeHeight    Current Stake channel height
         *  @param[in] nBits           Difficulty bits for the miner's channel
         *  @param[in] hashBestChain   Node's current best-chain hash (first 4 bytes used)
         *
         *  @return 28-byte payload vector
         *
         **/
        inline std::vector<uint8_t> BuildBestCurrentResponse(
            uint32_t nSessionId,
            uint32_t nUnifiedHeight,
            uint32_t nPrimeHeight,
            uint32_t nHashHeight,
            uint32_t nStakeHeight,
            uint32_t nBits,
            const uint1024_t& hashBestChain)
        {
            std::vector<uint8_t> v;
            v.reserve(28);

            /* [0..3] session_id (little-endian) */
            v.push_back(static_cast<uint8_t>(nSessionId & 0xFF));
            v.push_back(static_cast<uint8_t>((nSessionId >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>((nSessionId >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nSessionId >> 24) & 0xFF));

            /* [4..7] unified_height (big-endian) */
            v.push_back(static_cast<uint8_t>((nUnifiedHeight >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((nUnifiedHeight >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nUnifiedHeight >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(nUnifiedHeight & 0xFF));

            /* [8..11] prime_height (big-endian) */
            v.push_back(static_cast<uint8_t>((nPrimeHeight >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((nPrimeHeight >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nPrimeHeight >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(nPrimeHeight & 0xFF));

            /* [12..15] hash_height (big-endian) */
            v.push_back(static_cast<uint8_t>((nHashHeight >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((nHashHeight >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nHashHeight >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(nHashHeight & 0xFF));

            /* [16..19] stake_height (big-endian) */
            v.push_back(static_cast<uint8_t>((nStakeHeight >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((nStakeHeight >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nStakeHeight >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(nStakeHeight & 0xFF));

            /* [20..23] nBits (big-endian) */
            v.push_back(static_cast<uint8_t>((nBits >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>((nBits >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((nBits >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>(nBits & 0xFF));

            /* [24..27] hashBestChain_prefix: first 4 bytes of GetBytes() */
            std::vector<uint8_t> vHashBytes = hashBestChain.GetBytes();
            for(int i = 0; i < 4; ++i)
                v.push_back((i < static_cast<int>(vHashBytes.size())) ? vHashBytes[i] : 0x00);

            return v;
        }

    } /* namespace KeepaliveV2 */


    /** KeepAliveV2Frame
     *
     *  8-byte Miner→Node payload for the KEEPALIVE_V2 opcode.
     *
     *  Wire format (all little-endian uint32):
     *    [0-3]  sequence            Monotonic miner keepalive counter
     *    [4-7]  hashPrevBlock_lo32  Low 32 bits of miner's current prevHash (fork canary)
     *
     **/
    struct KeepAliveV2Frame
    {
        uint32_t sequence{0};
        uint32_t hashPrevBlock_lo32{0};

        static constexpr uint32_t PAYLOAD_SIZE = 8;

        /** Serialize — encode to wire bytes (little-endian) **/
        std::vector<uint8_t> Serialize() const
        {
            std::vector<uint8_t> v;
            v.reserve(PAYLOAD_SIZE);
            v.push_back(static_cast<uint8_t>(sequence & 0xFF));
            v.push_back(static_cast<uint8_t>((sequence >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>((sequence >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((sequence >> 24) & 0xFF));
            v.push_back(static_cast<uint8_t>(hashPrevBlock_lo32 & 0xFF));
            v.push_back(static_cast<uint8_t>((hashPrevBlock_lo32 >> 8) & 0xFF));
            v.push_back(static_cast<uint8_t>((hashPrevBlock_lo32 >> 16) & 0xFF));
            v.push_back(static_cast<uint8_t>((hashPrevBlock_lo32 >> 24) & 0xFF));
            return v;
        }

        /** Parse — decode from wire bytes; returns false if data is too short **/
        bool Parse(const std::vector<uint8_t>& data)
        {
            if(data.size() < PAYLOAD_SIZE)
                return false;
            sequence =
                static_cast<uint32_t>(data[0])         |
                (static_cast<uint32_t>(data[1]) << 8)  |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24);
            hashPrevBlock_lo32 =
                static_cast<uint32_t>(data[4])         |
                (static_cast<uint32_t>(data[5]) << 8)  |
                (static_cast<uint32_t>(data[6]) << 16) |
                (static_cast<uint32_t>(data[7]) << 24);
            return true;
        }
    };


    /** KeepAliveV2AckFrame
     *
     *  28-byte Node→Miner payload for the KEEPALIVE_V2_ACK opcode.
     *
     *  Feeds:
     *   1. Latent Fork Detection Manager  (hashPrevBlock_lo32 echo vs hash_tip_lo32)
     *   2. Unified Block Height Manager   (unified_height, prime_height, hash_height)
     *   3. Fork Resolution Manager        (fork_score)
     *
     *  Wire format (all little-endian uint32):
     *    [0-3]   sequence            Echo of miner's sequence
     *    [4-7]   hashPrevBlock_lo32  Echo of miner's prevHash canary
     *    [8-11]  unified_height      Node's unified block height
     *    [12-15] hash_tip_lo32       Low 32 bits of node's hashBestChain
     *    [16-19] prime_height        Node's Prime channel height
     *    [20-23] hash_height         Node's Hash channel height
     *    [24-27] fork_score          Latent Fork Detection score (0=healthy, >0=divergence)
     *
     **/
    struct KeepAliveV2AckFrame
    {
        uint32_t sequence{0};
        uint32_t hashPrevBlock_lo32{0};
        uint32_t unified_height{0};
        uint32_t hash_tip_lo32{0};
        uint32_t prime_height{0};
        uint32_t hash_height{0};
        uint32_t fork_score{0};

        static constexpr uint32_t PAYLOAD_SIZE = 28;

        /** Serialize — encode to wire bytes (little-endian) **/
        std::vector<uint8_t> Serialize() const
        {
            std::vector<uint8_t> v;
            v.reserve(PAYLOAD_SIZE);
            auto pushLE = [&](uint32_t val) {
                v.push_back(static_cast<uint8_t>(val & 0xFF));
                v.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                v.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                v.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
            };
            pushLE(sequence);
            pushLE(hashPrevBlock_lo32);
            pushLE(unified_height);
            pushLE(hash_tip_lo32);
            pushLE(prime_height);
            pushLE(hash_height);
            pushLE(fork_score);
            return v;
        }

        /** Parse — decode from wire bytes; returns false if data is too short **/
        bool Parse(const std::vector<uint8_t>& data)
        {
            if(data.size() < PAYLOAD_SIZE)
                return false;
            auto readLE = [&](size_t offset) -> uint32_t {
                return static_cast<uint32_t>(data[offset])         |
                       (static_cast<uint32_t>(data[offset+1]) << 8)  |
                       (static_cast<uint32_t>(data[offset+2]) << 16) |
                       (static_cast<uint32_t>(data[offset+3]) << 24);
            };
            sequence            = readLE(0);
            hashPrevBlock_lo32  = readLE(4);
            unified_height      = readLE(8);
            hash_tip_lo32       = readLE(12);
            prime_height        = readLE(16);
            hash_height         = readLE(20);
            fork_score          = readLE(24);
            return true;
        }
    };

} /* namespace LLP */

#endif /* NEXUS_LLP_INCLUDE_KEEPALIVE_V2_H */

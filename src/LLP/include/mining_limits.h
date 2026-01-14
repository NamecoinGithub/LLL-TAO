/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_MINING_LIMITS_H
#define NEXUS_LLP_INCLUDE_MINING_LIMITS_H

#include <cstdint>

namespace LLP
{
    /** Mining Protocol Safety Limits
     *
     *  These constants define maximum packet sizes for mining LLP connections
     *  to prevent protocol confusion and defend against malformed packets.
     *
     *  RATIONALE:
     *  - Auth packets: Falcon-1024 signatures (2560 bytes) + pubkey (1792 bytes) + metadata = ~5KB max
     *  - Block templates: Serialized TritiumBlock ~216 bytes for stateless, but legacy can be larger
     *  - General packets: Most mining packets are small (channel selection, keepalives, etc.)
     *
     *  These limits are intentionally generous to avoid false positives while still
     *  catching obviously bogus LENGTH values (like 478113 in the reported issue).
     **/
    namespace MiningLimits
    {
        /** Maximum size for authentication packets (Falcon auth, session init)
         *  
         *  Falcon-1024 worst case:
         *  - Public key: 1792 bytes
         *  - Signature: 2560 bytes  
         *  - Metadata (genesis hash, miner ID, etc.): 256 bytes
         *  Total: ~5KB with safety margin
         **/
        constexpr uint32_t MAX_AUTH_PACKET_LENGTH = 8192;  // 8 KB

        /** Maximum size for block-related packets (templates, submissions)
         *  
         *  Block submission packets can include:
         *  - Serialized block data (216-1024 bytes depending on transactions)
         *  - Proof of work data (nonce, merkle root): 72 bytes
         *  - Signature data: up to 2560 bytes for Falcon-1024
         *  - Safety margin for future extensions
         *  
         *  Note: Legacy SET_COINBASE can be up to 20KB (already limited in miner.cpp),
         *  but that's a different packet type.
         **/
        constexpr uint32_t MAX_BLOCK_PACKET_LENGTH = 24576;  // 24 KB

        /** Maximum size for general mining packets
         *  
         *  Covers most mining operations:
         *  - SET_CHANNEL (1-4 bytes)
         *  - GET_BLOCK (0 bytes)
         *  - GET_ROUND (0 bytes)
         *  - SESSION_KEEPALIVE (small payload)
         *  - Status queries and responses
         **/
        constexpr uint32_t MAX_GENERAL_PACKET_LENGTH = 4096;  // 4 KB

        /** Absolute maximum for any mining packet
         *  
         *  Hard limit to catch completely bogus LENGTH values.
         *  No legitimate mining packet should ever exceed this.
         *  
         *  This catches cases like the reported 478113 byte header.
         *  
         *  Note: This is the maximum for the packet payload (LENGTH field),
         *  not including the 6-byte LLP header (2-byte opcode + 4-byte length).
         *  Total on-wire bytes would be 32762 + 6 = 32768 (32 KB).
         **/
        constexpr uint32_t MAX_ANY_PACKET_LENGTH = 32762;  // 32 KB minus 6-byte header
    }

} // namespace LLP

#endif // NEXUS_LLP_INCLUDE_MINING_LIMITS_H
